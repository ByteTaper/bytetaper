// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_policy_plane.h"

#include "observability/logger.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/bootstrap_policy_importer.h"
#include "runtime_policy/startup_policy_loader.h"
#include "runtime_policy/startup_policy_validator.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_persistence.h"

#include <cstdio>
#include <memory>

namespace bytetaper::runtime_policy {

namespace {

std::shared_ptr<const runtime::RuntimePolicySnapshot>
build_empty_snapshot(runtime::RuntimePolicyStore* store) {
    const std::uint64_t next_gen = store->next_generation();
    auto build_res =
        runtime::build_runtime_policy_snapshot_from_routes(nullptr, 0, "none", next_gen);
    return build_res.snapshot;
}

std::shared_ptr<const runtime::RuntimePolicySnapshot>
build_inactive_snapshot(runtime::RuntimePolicyStore* store, bool reject_requests) {
    auto snapshot = build_empty_snapshot(store);
    if (!reject_requests) {
        return snapshot;
    }
    auto mutable_snapshot = std::const_pointer_cast<runtime::RuntimePolicySnapshot>(snapshot);
    mutable_snapshot->reject_requests = true;
    return mutable_snapshot;
}

std::shared_ptr<const runtime::RuntimePolicySnapshot>
build_snapshot_from_canonical_yaml(const std::string& canonical_yaml, std::uint64_t generation,
                                   std::string* error_out) {
    const taperquery::PolicyIrLoadResult load_res =
        taperquery::load_policy_ir_from_yaml_string(canonical_yaml.data(), canonical_yaml.size());
    if (!load_res.ok) {
        if (error_out != nullptr) {
            *error_out = "failed to parse committed canonical yaml: " + load_res.error;
        }
        return nullptr;
    }

    auto build_res = runtime::build_runtime_policy_snapshot_from_ir(load_res.policy, generation);
    if (!build_res.ok) {
        if (error_out != nullptr) {
            *error_out = "failed to build snapshot from committed yaml: " + build_res.error;
        }
        return nullptr;
    }
    return build_res.snapshot;
}

RuntimePolicyPlaneStartResult make_failure(RuntimePolicyStatus& status,
                                           RuntimePolicyDiagnostics& diagnostics,
                                           RuntimePolicyHealth health, const std::string& message,
                                           const char* error_code) {
    status.code = status_code_for_health(health);
    status.health = health;
    status.error_message = message;
    diagnostics.error_code = error_code;
    diagnostics.error_message = message;

    RuntimePolicyPlaneStartResult result{};
    result.ok = false;
    result.status = status;
    result.diagnostics = diagnostics;
    return result;
}

RuntimePolicyPlaneStartResult install_inactive_and_return(RuntimePolicyPlaneConfig& config,
                                                          RuntimePolicyStatus& status,
                                                          RuntimePolicyDiagnostics& diagnostics,
                                                          RuntimePolicyHealth health,
                                                          const std::string& message) {
    if (config.inactive_config.when_inactive == PolicyInactiveMode::StartupFail) {
        RuntimePolicyPlaneStartResult failure =
            make_failure(status, diagnostics, health, message, "policy_inactive_startup_fail");
        failure.status.code = RuntimePolicyStatusCode::StartupFailed;
        return failure;
    }

    const bool reject_requests = config.inactive_config.when_inactive == PolicyInactiveMode::Reject;

    std::string install_err;
    auto inactive_snapshot = build_inactive_snapshot(config.runtime_policy_store, reject_requests);
    if (!config.runtime_policy_store->install_initial(inactive_snapshot, &install_err)) {
        return make_failure(status, diagnostics, RuntimePolicyHealth::StartupFailed, install_err,
                            "install_initial_failed");
    }

    status.code = status_code_for_health(health);
    status.health = health;
    status.error_message = message;
    diagnostics.loaded_source = "inactive";
    diagnostics.error_code = "policy_inactive";
    diagnostics.error_message = message;

    RuntimePolicyPlaneStartResult result{};
    result.ok = true;
    result.status = status;
    result.diagnostics = diagnostics;
    return result;
}

void report_bootstrap_drift_if_present(RuntimePolicyPlaneConfig& config,
                                       RuntimePolicyDiagnostics& diagnostics,
                                       const control_plane::ActivePolicyPointer& pointer) {
    BootstrapImportInput drift_in{};
    drift_in.bootstrap_policy_file = config.bootstrap_policy_file;
    drift_in.store = config.policy_state_store;
    drift_in.resource_key = &config.resource_key;
    const RuntimePolicyHealth drift = check_bootstrap_drift(drift_in, pointer);
    if (drift == RuntimePolicyHealth::BootstrapDriftDetected) {
        diagnostics.bootstrap_drift_detected = true;
        diagnostics.error_code = "bootstrap_drift_detected";
        diagnostics.error_message =
            "bootstrap policy differs from committed active pointer; using committed policy";
        bytetaper::observability::log_info(
            "bootstrap policy drift detected relative to committed active pointer");
    }
}

} // namespace

RuntimePolicyPlane::RuntimePolicyPlane(RuntimePolicyPlaneConfig config)
    : config_(std::move(config)) {}

RuntimePolicyPlaneStartResult RuntimePolicyPlane::start() {
    diagnostics_ = RuntimePolicyDiagnostics{};
    diagnostics_.persistence_enabled = config_.persistence_config.enabled;
    diagnostics_.policy_state_dir = config_.persistence_config.state_dir;
    diagnostics_.bootstrap_policy_file =
        config_.bootstrap_policy_file ? config_.bootstrap_policy_file : "";

    if (config_.policy_state_store == nullptr) {
        const std::uint64_t next_gen = config_.runtime_policy_store->next_generation();
        StartupPolicyLoader loader(config_);
        StartupPolicyLoadResult load_result = loader.load(next_gen);
        diagnostics_ = load_result.diagnostics;

        if (!load_result.ok) {
            return make_failure(status_, diagnostics_, RuntimePolicyHealth::StartupFailed,
                                load_result.diagnostics.error_message, "legacy_startup_failed");
        }

        std::string install_err;
        if (!config_.runtime_policy_store->install_initial(load_result.snapshot, &install_err)) {
            return make_failure(status_, diagnostics_, RuntimePolicyHealth::StartupFailed,
                                install_err, "install_initial_failed");
        }

        status_.code = load_result.status_code;
        status_.health =
            load_result.status_code == RuntimePolicyStatusCode::LoadedFromPersistedPolicy
                ? RuntimePolicyHealth::Active
            : load_result.status_code == RuntimePolicyStatusCode::LoadedFromBootstrapPolicy
                ? RuntimePolicyHealth::BootstrapImported
            : load_result.status_code == RuntimePolicyStatusCode::LoadedEmptyPolicy
                ? RuntimePolicyHealth::NoPolicyConfigured
                : RuntimePolicyHealth::Unknown;

        RuntimePolicyPlaneStartResult result{};
        result.ok = true;
        result.status = status_;
        result.diagnostics = diagnostics_;
        return result;
    }

    const control_plane::LoadActivePointerResult pointer_res =
        config_.policy_state_store->load_active_pointer(config_.resource_key);

    if (pointer_res.ok) {
        taperquery::PolicyPersistenceLoadResult local_res{};
        const taperquery::PolicyPersistenceLoadResult* local_ptr = nullptr;
        bool validate_local_mirror = false;

        if (config_.persistence_config.enabled && !config_.persistence_config.state_dir.empty()) {
            local_res = taperquery::load_persisted_active_policy(config_.persistence_config);
            validate_local_mirror = !local_res.files_missing;
            local_ptr = &local_res;
        }

        StartupValidationInput validation_in{};
        validation_in.resource_key = &config_.resource_key;
        validation_in.active_pointer = &pointer_res.pointer;
        validation_in.local_load_result = local_ptr;
        validation_in.store = config_.policy_state_store;
        validation_in.validate_local_mirror = validate_local_mirror;

        const StartupValidationResult validation =
            validate_startup_against_active_pointer(validation_in);

        if (!validation.valid) {
            report_bootstrap_drift_if_present(config_, diagnostics_, pointer_res.pointer);
            return install_inactive_and_return(config_, status_, diagnostics_, validation.health,
                                               validation.message);
        }

        std::string build_err;
        auto snapshot = build_snapshot_from_canonical_yaml(
            validation.committed_canonical_yaml, pointer_res.pointer.generation, &build_err);
        if (snapshot == nullptr) {
            return make_failure(status_, diagnostics_, RuntimePolicyHealth::StartupFailed,
                                build_err, "committed_snapshot_build_failed");
        }

        std::string install_err;
        if (!config_.runtime_policy_store->install_initial(snapshot, &install_err)) {
            return make_failure(status_, diagnostics_, RuntimePolicyHealth::StartupFailed,
                                install_err, "install_initial_failed");
        }

        report_bootstrap_drift_if_present(config_, diagnostics_, pointer_res.pointer);

        status_.code = diagnostics_.bootstrap_drift_detected
                           ? RuntimePolicyStatusCode::BootstrapDriftDetected
                           : RuntimePolicyStatusCode::LoadedFromPersistedPolicy;
        status_.health = diagnostics_.bootstrap_drift_detected
                             ? RuntimePolicyHealth::BootstrapDriftDetected
                             : RuntimePolicyHealth::Active;
        diagnostics_.loaded_source = "policy_state_store";
        diagnostics_.policy_identity = pointer_res.pointer.policy_id;
        diagnostics_.generation = pointer_res.pointer.generation;

        RuntimePolicyPlaneStartResult result{};
        result.ok = true;
        result.status = status_;
        result.diagnostics = diagnostics_;
        return result;
    }

    if (!pointer_res.not_found) {
        return make_failure(status_, diagnostics_, RuntimePolicyHealth::StartupFailed,
                            pointer_res.error, "active_pointer_load_failed");
    }

    if (config_.bootstrap_policy_file != nullptr && config_.bootstrap_policy_file[0] != '\0') {
        BootstrapImportInput import_in{};
        import_in.bootstrap_policy_file = config_.bootstrap_policy_file;
        import_in.store = config_.policy_state_store;
        import_in.persistence_config = &config_.persistence_config;
        import_in.resource_key = &config_.resource_key;

        const BootstrapImportResult import_res = import_bootstrap_policy(import_in);
        if (!import_res.ok) {
            if (import_res.health == RuntimePolicyHealth::BootstrapDriftDetected) {
                return make_failure(status_, diagnostics_, import_res.health, import_res.error,
                                    "bootstrap_drift_detected");
            }
            return make_failure(status_, diagnostics_, import_res.health, import_res.error,
                                "bootstrap_import_failed");
        }

        std::string install_err;
        if (!config_.runtime_policy_store->install_initial(import_res.snapshot, &install_err)) {
            return make_failure(status_, diagnostics_, RuntimePolicyHealth::StartupFailed,
                                install_err, "install_initial_failed");
        }

        status_.code = RuntimePolicyStatusCode::BootstrapImported;
        status_.health = RuntimePolicyHealth::BootstrapImported;
        diagnostics_.loaded_source = "bootstrap_import";
        if (import_res.snapshot != nullptr) {
            diagnostics_.policy_identity = import_res.snapshot->policy_identity;
            diagnostics_.generation = import_res.snapshot->generation;
        }

        RuntimePolicyPlaneStartResult result{};
        result.ok = true;
        result.status = status_;
        result.diagnostics = diagnostics_;
        return result;
    }

    std::string install_err;
    auto empty_snapshot = build_empty_snapshot(config_.runtime_policy_store);
    if (!config_.runtime_policy_store->install_initial(empty_snapshot, &install_err)) {
        return make_failure(status_, diagnostics_, RuntimePolicyHealth::StartupFailed, install_err,
                            "install_initial_failed");
    }

    status_.code = RuntimePolicyStatusCode::NoPolicyConfigured;
    status_.health = RuntimePolicyHealth::NoPolicyConfigured;
    diagnostics_.loaded_source = "empty";

    RuntimePolicyPlaneStartResult result{};
    result.ok = true;
    result.status = status_;
    result.diagnostics = diagnostics_;
    return result;
}

const RuntimePolicyStatus& RuntimePolicyPlane::status() const {
    return status_;
}

const RuntimePolicyDiagnostics& RuntimePolicyPlane::diagnostics() const {
    return diagnostics_;
}

} // namespace bytetaper::runtime_policy

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/startup_policy_loader.h"

#include "observability/logger.h"
#include "policy/yaml_loader.h"
#include "runtime/policy_snapshot.h"
#include "taperquery/policy_persistence.h"

#include <cstdio>
#include <cstring>

// Generation number for bootstrap/empty snapshot — provided by caller via next_generation().
static constexpr std::uint64_t kBootstrapGeneration = 1;

namespace bytetaper::runtime_policy {

StartupPolicyLoader::StartupPolicyLoader(const RuntimePolicyPlaneConfig& config)
    : config_(config) {}

StartupPolicyLoadResult StartupPolicyLoader::load(std::uint64_t next_generation) {
    next_generation_ = next_generation;
    if (config_.persistence_config.enabled) {
        return try_load_persisted();
    }
    return load_bootstrap();
}

StartupPolicyLoadResult StartupPolicyLoader::try_load_persisted() {
    bytetaper::observability::log_info("checking for persisted active policy...");

    taperquery::StartupPolicyLoadConfig startup_cfg;
    startup_cfg.bootstrap_policy_file =
        config_.bootstrap_policy_file ? config_.bootstrap_policy_file : "";
    startup_cfg.policy_state_dir = config_.persistence_config.state_dir;
    startup_cfg.active_policy_filename = config_.persistence_config.active_policy_filename;
    startup_cfg.metadata_filename = config_.persistence_config.metadata_filename;
    startup_cfg.policy_persistence_enabled = config_.persistence_config.enabled;
    startup_cfg.fallback_to_bootstrap_on_persisted_policy_error = false;

    auto startup_res = taperquery::load_startup_policy_with_persistence(startup_cfg);
    if (!startup_res.ok) {
        std::fprintf(stderr,
                     "error: persisted active policy recovery failed due to corruption or "
                     "invalid configuration: %s\n",
                     startup_res.error.c_str());

        StartupPolicyLoadResult result{};
        result.ok = false;
        result.status_code = RuntimePolicyStatusCode::StartupFailed;
        result.diagnostics.loaded_source = "failed";
        result.diagnostics.persistence_enabled = true;
        result.diagnostics.policy_state_dir = config_.persistence_config.state_dir;
        result.diagnostics.bootstrap_policy_file =
            config_.bootstrap_policy_file ? config_.bootstrap_policy_file : "";
        result.diagnostics.error_code = "persisted_policy_corrupt";
        result.diagnostics.error_message = startup_res.error;
        return result;
    }

    if (startup_res.loaded_source == "persisted") {
        char log_msg[512];
        std::snprintf(log_msg, sizeof(log_msg),
                      "restored active policy from disk: identity=%s, generation=%zu",
                      startup_res.policy_identity.c_str(),
                      static_cast<std::size_t>(startup_res.generation));
        bytetaper::observability::log_info(log_msg);

        auto build_res = runtime::build_runtime_policy_snapshot_from_ir(startup_res.policy_ir,
                                                                        startup_res.generation);
        if (!build_res.ok) {
            std::fprintf(stderr, "error: failed to build snapshot from recovered policy: %s\n",
                         build_res.error.c_str());

            StartupPolicyLoadResult result{};
            result.ok = false;
            result.status_code = RuntimePolicyStatusCode::StartupFailed;
            result.diagnostics.loaded_source = "failed";
            result.diagnostics.persistence_enabled = true;
            result.diagnostics.policy_state_dir = config_.persistence_config.state_dir;
            result.diagnostics.error_code = "snapshot_build_failed";
            result.diagnostics.error_message = build_res.error;
            return result;
        }

        StartupPolicyLoadResult result{};
        result.ok = true;
        result.status_code = RuntimePolicyStatusCode::LoadedFromPersistedPolicy;
        result.snapshot = build_res.snapshot;
        result.diagnostics.loaded_source = "persisted";
        result.diagnostics.policy_identity = startup_res.policy_identity;
        result.diagnostics.generation = startup_res.generation;
        result.diagnostics.persistence_enabled = true;
        result.diagnostics.policy_state_dir = config_.persistence_config.state_dir;
        result.diagnostics.bootstrap_policy_file =
            config_.bootstrap_policy_file ? config_.bootstrap_policy_file : "";
        return result;
    }

    // No persisted file found — fall through to bootstrap
    bytetaper::observability::log_info(
        "no persisted active policy found (will fall back to bootstrap policy)");
    return load_bootstrap();
}

StartupPolicyLoadResult StartupPolicyLoader::load_bootstrap() {
    if (config_.bootstrap_policy_file == nullptr) {
        return load_empty();
    }

    policy::PolicyFileResult policy_result{};
    if (!policy::load_policy_from_file(config_.bootstrap_policy_file, &policy_result)) {
        std::fprintf(stderr, "failed to load policy file %s: %s\n", config_.bootstrap_policy_file,
                     policy_result.error ? policy_result.error : "unknown error");

        StartupPolicyLoadResult result{};
        result.ok = false;
        result.status_code = RuntimePolicyStatusCode::StartupFailed;
        result.diagnostics.loaded_source = "failed";
        result.diagnostics.persistence_enabled = config_.persistence_config.enabled;
        result.diagnostics.policy_state_dir = config_.persistence_config.state_dir;
        result.diagnostics.bootstrap_policy_file = config_.bootstrap_policy_file;
        result.diagnostics.error_code = "bootstrap_load_failed";
        result.diagnostics.error_message =
            policy_result.error ? policy_result.error : "unknown error";
        return result;
    }

    if (policy_result.warning[0] != '\0') {
        std::fprintf(stderr, "policy warning: %s\n", policy_result.warning);
    }

    char buf[512];
    std::snprintf(buf, sizeof(buf), "loaded %zu routes from %s", policy_result.count,
                  config_.bootstrap_policy_file);
    bytetaper::observability::log_info(buf);

    auto build_res = runtime::build_runtime_policy_snapshot_from_routes(
        policy_result.policies, policy_result.count, config_.bootstrap_policy_file,
        next_generation_);

    if (!build_res.ok) {
        std::fprintf(stderr, "failed to build initial policy snapshot: %s\n",
                     build_res.error.c_str());

        StartupPolicyLoadResult result{};
        result.ok = false;
        result.status_code = RuntimePolicyStatusCode::StartupFailed;
        result.diagnostics.loaded_source = "failed";
        result.diagnostics.persistence_enabled = config_.persistence_config.enabled;
        result.diagnostics.bootstrap_policy_file = config_.bootstrap_policy_file;
        result.diagnostics.error_code = "snapshot_build_failed";
        result.diagnostics.error_message = build_res.error;
        return result;
    }

    StartupPolicyLoadResult result{};
    result.ok = true;
    result.status_code = RuntimePolicyStatusCode::LoadedFromBootstrapPolicy;
    result.snapshot = build_res.snapshot;
    result.diagnostics.loaded_source = "bootstrap";
    result.diagnostics.policy_identity =
        build_res.snapshot ? build_res.snapshot->policy_identity : "";
    result.diagnostics.generation = build_res.snapshot ? build_res.snapshot->generation : 0;
    result.diagnostics.persistence_enabled = config_.persistence_config.enabled;
    result.diagnostics.policy_state_dir = config_.persistence_config.state_dir;
    result.diagnostics.bootstrap_policy_file = config_.bootstrap_policy_file;
    return result;
}

StartupPolicyLoadResult StartupPolicyLoader::load_empty() {
    auto build_res =
        runtime::build_runtime_policy_snapshot_from_routes(nullptr, 0, "none", next_generation_);

    StartupPolicyLoadResult result{};
    result.ok = true;
    result.status_code = RuntimePolicyStatusCode::LoadedEmptyPolicy;
    result.snapshot = build_res.snapshot;
    result.diagnostics.loaded_source = "empty";
    result.diagnostics.policy_identity =
        build_res.snapshot ? build_res.snapshot->policy_identity : "";
    result.diagnostics.generation = build_res.snapshot ? build_res.snapshot->generation : 0;
    result.diagnostics.persistence_enabled = config_.persistence_config.enabled;
    result.diagnostics.policy_state_dir = config_.persistence_config.state_dir;
    result.diagnostics.bootstrap_policy_file =
        config_.bootstrap_policy_file ? config_.bootstrap_policy_file : "";
    return result;
}

} // namespace bytetaper::runtime_policy

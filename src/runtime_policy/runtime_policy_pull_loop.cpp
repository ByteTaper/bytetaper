// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_policy_pull_loop.h"

#include "control_plane/policy_lifecycle_event.h"
#include "operational/policy_activation_result.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/policy_mismatch_classifier.h"
#include "runtime_policy/runtime_policy_log_events.h"
#include "runtime_policy/runtime_policy_metrics.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <chrono>
#include <cstring>

namespace bytetaper::runtime_policy {

namespace {

std::string normalize_canonical_hash(const std::string& hash) {
    if (hash.rfind("sha256:", 0) == 0) {
        return hash;
    }
    if (hash.empty()) {
        return hash;
    }
    return "sha256:" + hash;
}

std::string canonical_hash_for_policy_ir(const taperquery::TqPolicyDocument& policy_ir) {
    const taperquery::PolicyIrYamlEmitResult emit =
        taperquery::emit_policy_ir_canonical_yaml(policy_ir);
    if (!emit.ok || emit.yaml.empty()) {
        return "";
    }
    return "sha256:" + taperquery::compute_canonical_yaml_sha256_hex(emit.yaml);
}

} // namespace

RuntimePolicyPullLoop::RuntimePolicyPullLoop(RuntimePolicyPullLoopConfig config)
    : config_(std::move(config)) {}

void RuntimePolicyPullLoop::start() {
    if (running_.load()) {
        return;
    }
    stop_.store(false);
    update_status(RuntimePolicyPullState::Starting);
    running_.store(true);
    thread_ = std::thread([this]() { loop_main(); });
}

void RuntimePolicyPullLoop::stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
    update_status(RuntimePolicyPullState::Stopped);
}

bool RuntimePolicyPullLoop::is_running() const {
    return running_.load();
}

std::int64_t RuntimePolicyPullLoop::now_unix_epoch_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::uint32_t RuntimePolicyPullLoop::sleep_ms_for_failures() const {
    std::uint32_t failures = 0;
    {
        std::lock_guard<std::mutex> lock(status_mu_);
        failures = status_.consecutive_failures;
    }
    if (failures == 0) {
        return config_.pull.pull_interval_ms;
    }
    std::uint32_t backoff = config_.pull.pull_interval_ms;
    for (std::uint32_t i = 1; i < failures && backoff < 60000u; ++i) {
        backoff *= 2;
    }
    if (backoff > 60000u) {
        backoff = 60000u;
    }
    return backoff;
}

void RuntimePolicyPullLoop::update_status(RuntimePolicyPullState state) {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_.state = state;
}

void RuntimePolicyPullLoop::record_failure(const std::string& code, const std::string& message) {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_.last_error_code = code;
    status_.last_control_plane_error = message;
    status_.consecutive_failures++;
    if (code == kErrControlPlaneUnavailable || code == kErrActivePolicyQueryFailed) {
        status_.control_plane_reachable = false;
        status_.state = RuntimePolicyPullState::DegradedControlPlaneUnavailable;
        status_.activation_status = "control_plane_unavailable";
    } else {
        status_.state = RuntimePolicyPullState::ActivationFailed;
        status_.activation_status = "activation_failed";
    }
}

void RuntimePolicyPullLoop::sync_status_from_local_snapshot() {
    if (config_.runtime_policy_store == nullptr) {
        return;
    }

    const auto snapshot = config_.runtime_policy_store->load();
    if (snapshot == nullptr) {
        return;
    }

    const std::string canonical_hash = canonical_hash_for_policy_ir(snapshot->policy_ir);

    std::lock_guard<std::mutex> lock(status_mu_);
    status_.active_generation = snapshot->generation;
    status_.active_policy_id = snapshot->policy_identity;
    if (!canonical_hash.empty()) {
        status_.active_canonical_hash = canonical_hash;
    }
}

void RuntimePolicyPullLoop::report_status_to_control_plane() {
    if (config_.client == nullptr) {
        return;
    }
    sync_status_from_local_snapshot();
    {
        std::lock_guard<std::mutex> lock(status_mu_);
        if (status_.activation_status == "policy_inactive") {
            if (status_.active_policy_id.empty()) {
                status_.active_policy_id = "policy_inactive";
            }
            if (status_.active_canonical_hash.empty()) {
                status_.active_canonical_hash = "sha256:policy_inactive";
            }
        }
    }
    (void) config_.client->report_runtime_status(build_report());
}

void RuntimePolicyPullLoop::reset_failures() {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_.consecutive_failures = 0;
    status_.last_error_code.clear();
    status_.last_control_plane_error.clear();
    status_.control_plane_reachable = true;
}

RuntimePolicyPullStatus RuntimePolicyPullLoop::status() const {
    std::lock_guard<std::mutex> lock(status_mu_);
    return status_;
}

RuntimePolicyStatusReport RuntimePolicyPullLoop::build_report() const {
    const RuntimePolicyPullStatus st = status();
    RuntimePolicyStatusReport report{};
    report.runtime_id = config_.pull.runtime_id;
    report.resource_key = config_.pull.resource_key.to_string();
    report.active_generation = st.active_generation;
    report.active_policy_id = st.active_policy_id;
    report.active_canonical_hash = st.active_canonical_hash;
    report.activation_status = st.activation_status;
    report.control_plane_reachable = st.control_plane_reachable;
    report.last_pull_at_unix_epoch_ms = st.last_control_plane_check_at_unix_epoch_ms;
    report.last_activated_at_unix_epoch_ms = st.last_successful_activation_at_unix_epoch_ms;
    report.gateway_adapter = config_.pull.gateway_adapter;
    report.data_path_mode = config_.pull.data_path_mode;
    report.last_error_code = st.last_error_code;
    report.last_error_message = st.last_control_plane_error;
    return report;
}

void RuntimePolicyPullLoop::tick() {
    if (config_.client == nullptr || config_.runtime_policy_store == nullptr) {
        record_failure(kErrControlPlaneUnavailable, "pull loop client or runtime store missing");
        return;
    }

    update_status(RuntimePolicyPullState::CheckingControlPlane);

    const ActivePolicyPointerResult active_res =
        config_.client->get_active_policy(config_.pull.resource_key);

    {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.last_control_plane_check_at_unix_epoch_ms = now_unix_epoch_ms();
    }

    if (!active_res.ok) {
        record_failure(active_res.error_code.empty() ? kErrActivePolicyQueryFailed
                                                     : active_res.error_code,
                       active_res.error);
        const auto snapshot = config_.runtime_policy_store->load();
        if (snapshot == nullptr || snapshot->generation == 0) {
            {
                std::lock_guard<std::mutex> lock(status_mu_);
                status_.activation_status = "policy_inactive";
            }
            update_status(RuntimePolicyPullState::PolicyInactive);
        }
        report_status_to_control_plane();
        return;
    }

    reset_failures();

    const control_plane::ActivePolicyPointer& remote = active_res.active;

    const auto local_snapshot = config_.runtime_policy_store->load();
    std::uint64_t local_generation = 0;
    std::string local_policy_id;
    std::string local_canonical_hash;
    {
        std::lock_guard<std::mutex> lock(status_mu_);
        local_canonical_hash = status_.active_canonical_hash;
    }
    if (local_snapshot != nullptr) {
        local_generation = local_snapshot->generation;
        local_policy_id = local_snapshot->policy_identity;
    }

    const std::string remote_hash = normalize_canonical_hash(remote.canonical_hash);

    const bool hash_matches =
        local_canonical_hash.empty() || remote_hash.empty() || local_canonical_hash == remote_hash;

    if (local_generation == remote.generation && local_policy_id == remote.policy_id &&
        hash_matches) {
        update_status(RuntimePolicyPullState::Idle);
        {
            std::lock_guard<std::mutex> lock(status_mu_);
            status_.active_generation = local_generation;
            status_.active_policy_id = local_policy_id;
            status_.active_canonical_hash = remote_hash;
            status_.activation_status = "active";
            status_.state = RuntimePolicyPullState::Active;
        }
        if (config_.lifecycle_emitter != nullptr || config_.runtime_policy_metrics != nullptr) {
            control_plane::PolicyLifecycleEvent event{};
            event.event_type = control_plane::PolicyLifecycleEventType::RuntimeConverged;
            event.resource_key = config_.pull.resource_key.to_string();
            event.after_generation = remote.generation;
            event.after_policy_id = remote.policy_id;
            event.status = "success";
            if (config_.lifecycle_emitter != nullptr) {
                (void) config_.lifecycle_emitter->emit(event);
            }
            record_runtime_convergence(config_.runtime_policy_metrics, "converged");
            log_runtime_policy_lifecycle_event(event);
        }
        report_status_to_control_plane();
        return;
    }

    if ((config_.lifecycle_emitter != nullptr || config_.runtime_policy_metrics != nullptr) &&
        local_generation > 0 && local_generation < remote.generation) {
        control_plane::PolicyLifecycleEvent event{};
        event.event_type = control_plane::PolicyLifecycleEventType::RuntimeStale;
        event.resource_key = config_.pull.resource_key.to_string();
        event.before_generation = local_generation;
        event.after_generation = remote.generation;
        event.status = "failure";
        event.message = "stale";
        if (config_.lifecycle_emitter != nullptr) {
            (void) config_.lifecycle_emitter->emit(event);
        }
        record_runtime_convergence(config_.runtime_policy_metrics, "stale");
        log_runtime_policy_lifecycle_event(event);
    }

    update_status(RuntimePolicyPullState::FetchingPolicy);

    const CanonicalPolicyResult version_res =
        config_.client->get_policy_version(config_.pull.resource_key, remote.generation);
    if (!version_res.ok) {
        record_failure(version_res.error_code.empty() ? kErrPolicyVersionFetchFailed
                                                      : version_res.error_code,
                       version_res.error);
        report_status_to_control_plane();
        return;
    }

    update_status(RuntimePolicyPullState::ValidatingPolicy);

    const std::string computed_hash =
        "sha256:" + taperquery::compute_canonical_yaml_sha256_hex(version_res.canonical_yaml);
    if (computed_hash != remote_hash) {
        record_failure(kErrPolicyCanonicalHashMismatch,
                       "fetched policy canonical hash does not match active pointer");
        report_status_to_control_plane();
        return;
    }

    const taperquery::PolicyIrLoadResult load_res = taperquery::load_policy_ir_from_yaml_string(
        version_res.canonical_yaml.data(), version_res.canonical_yaml.size());
    if (!load_res.ok) {
        record_failure(kErrPolicyCompileFailed, load_res.error);
        report_status_to_control_plane();
        return;
    }

    const std::string computed_policy_id =
        taperquery::compute_policy_document_identity(load_res.policy);
    if (computed_policy_id != remote.policy_id) {
        record_failure(kErrPolicyIdMismatch,
                       "fetched policy identity does not match active pointer policy_id");
        report_status_to_control_plane();
        return;
    }

    if (version_res.record.generation != 0 && version_res.record.generation != remote.generation) {
        record_failure(kErrPolicyGenerationMismatch,
                       "fetched policy version record generation does not match active pointer");
        report_status_to_control_plane();
        return;
    }

    const runtime::RuntimePolicySnapshotBuildResult build_res =
        config_.snapshot_build_fn != nullptr
            ? config_.snapshot_build_fn(load_res.policy, remote.generation)
            : runtime::build_runtime_policy_snapshot_from_ir(load_res.policy, remote.generation);
    if (!build_res.ok || build_res.snapshot == nullptr) {
        record_failure(kErrPolicyCompileFailed,
                       build_res.error.empty() ? "runtime snapshot build failed" : build_res.error);
        report_status_to_control_plane();
        return;
    }

    update_status(RuntimePolicyPullState::ActivatingPolicy);

    operational::PolicyActivationRequest activation_req{};
    activation_req.generation = remote.generation;
    activation_req.policy_id = remote.policy_id;
    activation_req.previous_generation = remote.previous_generation > 0
                                             ? remote.previous_generation
                                             : (local_generation > 0 ? local_generation : 0);
    activation_req.committed_policy_ir = &load_res.policy;
    activation_req.committed_snapshot = build_res.snapshot;

    operational::PolicyActivationBarrierConfig activation_cfg = config_.activation_barrier;
    activation_cfg.lifecycle_emitter = config_.lifecycle_emitter;
    activation_cfg.runtime_policy_metrics = config_.runtime_policy_metrics;
    operational::PolicyActivationBarrier barrier_with_obs(activation_cfg);
    const operational::PolicyActivationResult activation =
        barrier_with_obs.activate(activation_req);
    if (!activation.ok) {
        {
            control_plane::PolicyLifecycleEvent event{};
            event.event_type = control_plane::PolicyLifecycleEventType::RuntimeActivationFailed;
            event.resource_key = config_.pull.resource_key.to_string();
            event.after_generation = remote.generation;
            event.stage = operational::to_string(activation.failed_stage);
            event.status = "failure";
            event.error_code = activation.error_code;
            event.message = activation.message;
            if (config_.lifecycle_emitter != nullptr) {
                (void) config_.lifecycle_emitter->emit(event);
            }
            record_runtime_convergence(config_.runtime_policy_metrics, "failed");
            log_runtime_policy_lifecycle_event(event);
        }
        record_failure(kErrPolicyActivationFailed, activation.message);
        report_status_to_control_plane();
        return;
    }

    if (config_.pull.local_mirror.enabled && !config_.pull.local_mirror.state_dir.empty()) {
        taperquery::PersistedPolicyMetadata metadata{};
        metadata.policy_identity = remote.policy_id;
        metadata.generation = remote.generation;
        metadata.canonical_hash = remote_hash;
        metadata.canonical_yaml_sha256 = strip_sha256_prefix(remote_hash);
        metadata.canonical_hash_algorithm = "sha256";
        metadata.resource_key = config_.pull.resource_key.to_string();
        metadata.metadata_schema_version = 1;
        metadata.schema_version =
            remote.schema_version != 0 ? remote.schema_version : version_res.record.schema_version;
        metadata.api_version =
            remote.api_version.empty() ? version_res.record.api_version : remote.api_version;
        metadata.kind = remote.kind.empty() ? version_res.record.kind : remote.kind;
        metadata.source_type =
            remote.source_type.empty() ? version_res.record.source_type : remote.source_type;
        if (metadata.schema_version == 0 && load_res.policy.schema_version_num != 0) {
            metadata.schema_version = load_res.policy.schema_version_num;
        }
        if (metadata.api_version.empty() && !load_res.policy.api_version.empty()) {
            metadata.api_version = load_res.policy.api_version;
        }
        if (metadata.kind.empty() && !load_res.policy.kind.empty()) {
            metadata.kind = load_res.policy.kind;
        }
        if (metadata.source_type.empty()) {
            metadata.source_type = "taperql-apply";
        }
        metadata.active_policy_file = config_.pull.local_mirror.active_policy_filename.empty()
                                          ? "active-policy.yaml"
                                          : config_.pull.local_mirror.active_policy_filename;
        metadata.committed_at_unix_epoch_ms = remote.committed_at_unix_epoch_ms != 0
                                                  ? remote.committed_at_unix_epoch_ms
                                                  : version_res.record.created_at_unix_epoch_ms;
        metadata.written_at_unix_epoch_ms = static_cast<std::uint64_t>(now_unix_epoch_ms());

        const taperquery::PolicyPersistenceWriteResult persist_res =
            taperquery::persist_active_policy_canonical_yaml(config_.pull.local_mirror,
                                                             load_res.policy, metadata);
        if (!persist_res.ok) {
            std::lock_guard<std::mutex> lock(status_mu_);
            status_.last_error_code = kErrLocalMirrorWriteFailed;
            status_.last_control_plane_error = persist_res.error;
        }
    }

    {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.active_generation = remote.generation;
        status_.active_policy_id = remote.policy_id;
        status_.active_canonical_hash = remote_hash;
        status_.activation_status = operational::to_string(activation.status);
        status_.last_successful_activation_at_unix_epoch_ms = now_unix_epoch_ms();
        status_.state = RuntimePolicyPullState::Active;
    }

    report_status_to_control_plane();
}

void RuntimePolicyPullLoop::loop_main() {
    while (!stop_.load()) {
        tick();
        const std::uint32_t sleep_ms = sleep_ms_for_failures();
        for (std::uint32_t elapsed = 0; elapsed < sleep_ms && !stop_.load(); elapsed += 50) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

} // namespace bytetaper::runtime_policy

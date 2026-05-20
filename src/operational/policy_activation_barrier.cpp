// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/policy_activation_barrier.h"

#include "control_plane/policy_lifecycle_event.h"
#include "operational/cache_namespace_sync.h"
#include "operational/materialized_variant_sync.h"
#include "operational/policy_activation_result.h"
#include "operational/policy_cleanup_sync.h"
#include "operational/policy_operational_diff.h"
#include "operational/route_epoch_sync.h"
#include "runtime_policy/runtime_policy_log_events.h"
#include "runtime_policy/runtime_policy_metrics.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"

#include <chrono>

namespace bytetaper::operational {

namespace {

void emit_activation_event(const PolicyActivationBarrierConfig& config,
                           const PolicyActivationRequest& request,
                           control_plane::PolicyLifecycleEventType type, const std::string& status,
                           const char* stage, const std::string& error_code,
                           const std::string& message, std::uint64_t duration_ms,
                           bool old_snapshot_still_active, bool cleanup_pending) {
    control_plane::PolicyLifecycleEmitter* emitter = config.lifecycle_emitter;
    const control_plane::PolicyResourceKey& key = config.resource_key;
    control_plane::PolicyLifecycleEvent event{};
    event.event_type = type;
    event.resource_key = key.to_string();
    event.before_generation = request.previous_generation;
    event.after_generation = request.generation;
    event.before_policy_id = "";
    event.after_policy_id = request.policy_id;
    event.policy_id = request.policy_id;
    event.status = status;
    event.stage = stage != nullptr ? stage : "";
    event.error_code = error_code;
    event.message = message;
    event.duration_ms = duration_ms;
    event.old_snapshot_still_active = old_snapshot_still_active;
    event.manual_resolution_required = !status.empty() && status == "failure";
    if (cleanup_pending) {
        event.message = event.message.empty() ? "cleanup_pending" : event.message;
    }
    if (emitter != nullptr) {
        (void) emitter->emit(event);
    }
    if (config.runtime_policy_metrics != nullptr) {
        if (type == control_plane::PolicyLifecycleEventType::PolicyActivationSucceeded) {
            runtime_policy::record_activation_success(config.runtime_policy_metrics, duration_ms,
                                                      request.generation);
        } else if (type == control_plane::PolicyLifecycleEventType::PolicyActivationFailed) {
            runtime_policy::record_activation_failure(config.runtime_policy_metrics, stage);
        }
    }
    if (type == control_plane::PolicyLifecycleEventType::PolicyActivationStarted ||
        type == control_plane::PolicyLifecycleEventType::PolicyActivationSucceeded ||
        type == control_plane::PolicyLifecycleEventType::PolicyActivationFailed) {
        runtime_policy::log_runtime_policy_lifecycle_event(event);
    }
}

PolicyActivationResult make_failure(PolicyActivationResult result, PolicyActivationStage stage,
                                    const std::string& code, const std::string& message,
                                    bool old_snapshot_still_active) {
    result.ok = false;
    result.status = PolicyActivationStatus::PolicyActivationFailed;
    result.stage = stage;
    result.failed_stage = stage;
    result.error_code = code;
    result.message = message;
    result.old_snapshot_still_active = old_snapshot_still_active;
    return result;
}

PolicyActivationResult fail_activation(PolicyActivationResult result, PolicyActivationStage stage,
                                       const std::string& code, const std::string& message,
                                       bool old_snapshot_still_active,
                                       const PolicyActivationBarrierConfig& config,
                                       const PolicyActivationRequest& request,
                                       const std::chrono::steady_clock::time_point& start,
                                       const std::chrono::steady_clock::time_point& stage_clock) {
    if (config.runtime_policy_metrics != nullptr) {
        const std::uint64_t stage_ms =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - stage_clock)
                                           .count());
        runtime_policy::record_activation_stage_duration(
            config.runtime_policy_metrics, runtime_policy::policy_activation_stage_index(stage),
            stage_ms);
    }
    result = make_failure(std::move(result), stage, code, message, old_snapshot_still_active);
    const auto duration_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count());
    emit_activation_event(config, request,
                          control_plane::PolicyLifecycleEventType::PolicyActivationFailed,
                          "failure", to_string(stage), result.error_code, result.message,
                          duration_ms, result.old_snapshot_still_active, false);
    return result;
}

bool load_policy_ir_from_store(control_plane::PolicyStateStore* store,
                               const control_plane::PolicyResourceKey& key,
                               std::uint64_t generation, taperquery::TqPolicyDocument* out,
                               std::string* error) {
    const control_plane::LoadPolicyVersionResult loaded =
        store->load_policy_version(key, generation);
    if (!loaded.ok) {
        if (error != nullptr) {
            *error = loaded.error.empty() ? "policy version not found" : loaded.error;
        }
        return false;
    }

    const taperquery::PolicyIrLoadResult ir = taperquery::load_policy_ir_from_yaml_string(
        loaded.canonical_yaml.data(), loaded.canonical_yaml.size());
    if (!ir.ok) {
        if (error != nullptr) {
            *error = ir.error;
        }
        return false;
    }

    *out = ir.policy;
    return true;
}

std::uint32_t count_l1_jobs(const taperquery::TqCacheNamespaceApplyResult& epoch_res) {
    std::uint32_t count = 0;
    for (const auto& route : epoch_res.routes) {
        if (route.l1_cleanup_required) {
            count++;
        }
    }
    return count;
}

std::uint32_t count_l2_jobs(const taperquery::TqCacheNamespaceApplyResult& epoch_res) {
    std::uint32_t count = 0;
    for (const auto& route : epoch_res.routes) {
        if (route.l2_cleanup_enqueued) {
            count++;
        }
    }
    return count;
}

} // namespace

void record_completed_activation_stage(const PolicyActivationBarrierConfig& config,
                                       PolicyActivationStage stage,
                                       std::chrono::steady_clock::time_point* stage_clock) {
    if (config.runtime_policy_metrics == nullptr || stage_clock == nullptr) {
        return;
    }
    const std::uint64_t duration_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - *stage_clock)
                                       .count());
    runtime_policy::record_activation_stage_duration(
        config.runtime_policy_metrics, runtime_policy::policy_activation_stage_index(stage),
        duration_ms);
    *stage_clock = std::chrono::steady_clock::now();
}

PolicyActivationBarrier::PolicyActivationBarrier(PolicyActivationBarrierConfig config)
    : config_(std::move(config)) {}

PolicyActivationResult PolicyActivationBarrier::activate(const PolicyActivationRequest& request) {
    const auto activation_start = std::chrono::steady_clock::now();
    auto stage_clock = std::chrono::steady_clock::now();
    PolicyActivationResult result{};
    result.resource_key = config_.resource_key.to_string();
    result.generation = request.generation;
    result.policy_id = request.policy_id;
    result.status = PolicyActivationStatus::PolicyActivationInProgress;
    result.stage = PolicyActivationStage::Committed;

    emit_activation_event(config_, request,
                          control_plane::PolicyLifecycleEventType::PolicyActivationStarted,
                          "success", "started", "", "", 0, false, false);

    if (config_.policy_state_store == nullptr || config_.runtime_policy_store == nullptr) {
        return fail_activation(std::move(result), PolicyActivationStage::Committed,
                               "POLICY_ACTIVATION_STORE_UNAVAILABLE",
                               "policy state store or runtime policy store is not configured", true,
                               config_, request, activation_start, stage_clock);
    }

    taperquery::TqPolicyDocument before_ir{};
    taperquery::TqPolicyDocument after_ir{};

    const std::uint64_t before_generation =
        request.previous_generation > 0 ? request.previous_generation : request.generation - 1;

    if (before_generation > 0) {
        std::string load_err;
        if (!load_policy_ir_from_store(config_.policy_state_store, config_.resource_key,
                                       before_generation, &before_ir, &load_err)) {
            return fail_activation(std::move(result), PolicyActivationStage::Committed,
                                   "POLICY_ACTIVATION_LOAD_BEFORE_FAILED",
                                   "failed to load previous policy version: " + load_err, true,
                                   config_, request, activation_start, stage_clock);
        }
    }

    if (request.committed_policy_ir != nullptr) {
        after_ir = *request.committed_policy_ir;
        const std::string computed_policy_id =
            taperquery::compute_policy_document_identity(after_ir);
        if (computed_policy_id != request.policy_id) {
            return fail_activation(std::move(result), PolicyActivationStage::Committed,
                                   "POLICY_ACTIVATION_POLICY_ID_MISMATCH",
                                   "committed policy identity does not match request policy_id",
                                   true, config_, request, activation_start, stage_clock);
        }
    } else {
        std::string after_load_err;
        if (!load_policy_ir_from_store(config_.policy_state_store, config_.resource_key,
                                       request.generation, &after_ir, &after_load_err)) {
            return fail_activation(std::move(result), PolicyActivationStage::Committed,
                                   "POLICY_ACTIVATION_LOAD_COMMITTED_FAILED",
                                   "failed to load committed policy version: " + after_load_err,
                                   true, config_, request, activation_start, stage_clock);
        }
    }

    taperquery::TqApplyPlanOptions plan_opts{};
    plan_opts.include_field_level_changes = true;
    const PolicyOperationalDiffResult diff =
        compute_policy_operational_diff(before_ir, after_ir, plan_opts);
    record_completed_activation_stage(config_, PolicyActivationStage::Committed, &stage_clock);
    result.stage = PolicyActivationStage::OperationalDiffComputed;
    result.affected_routes = diff.affected_routes;

    if (config_.route_cache_epoch_store != nullptr) {
        const auto impact =
            detect_cache_namespace_impacts_for_plan(diff.plan, config_.route_cache_epoch_store);
        if (!impact.ok) {
            return fail_activation(std::move(result),
                                   PolicyActivationStage::OperationalDiffComputed,
                                   "POLICY_ACTIVATION_CACHE_NAMESPACE_FAILED", impact.error, true,
                                   config_, request, activation_start, stage_clock);
        }
    }
    record_completed_activation_stage(config_, PolicyActivationStage::OperationalDiffComputed,
                                      &stage_clock);
    result.stage = PolicyActivationStage::CacheNamespacePrepared;

    taperquery::TqCacheNamespaceApplyResult epoch_res;
    if (config_.route_cache_epoch_store != nullptr) {
        epoch_res = sync_route_epochs_for_apply(diff.plan, config_.route_cache_epoch_store);
        if (!epoch_res.ok) {
            return fail_activation(std::move(result), PolicyActivationStage::RouteEpochsBumped,
                                   "POLICY_ACTIVATION_EPOCH_BUMP_FAILED", epoch_res.error, true,
                                   config_, request, activation_start, stage_clock);
        }
    } else {
        epoch_res.ok = true;
    }
    record_completed_activation_stage(config_, PolicyActivationStage::CacheNamespacePrepared,
                                      &stage_clock);
    result.stage = PolicyActivationStage::RouteEpochsBumped;
    result.bumped_route_epochs = count_bumped_route_epochs(epoch_res);

    const PolicyCleanupSyncResult cleanup =
        ::bytetaper::operational::prepare_operational_sync_for_apply(epoch_res, config_.l1_cache,
                                                                     config_.l2_cleanup_queue);
    if (!cleanup.epoch_res.ok) {
        rollback_route_epochs_for_apply(config_.route_cache_epoch_store, epoch_res);
        return fail_activation(std::move(result), PolicyActivationStage::CleanupEnqueued,
                               "POLICY_ACTIVATION_CLEANUP_FAILED", cleanup.epoch_res.error, true,
                               config_, request, activation_start, stage_clock);
    }
    record_completed_activation_stage(config_, PolicyActivationStage::RouteEpochsBumped,
                                      &stage_clock);
    result.stage = PolicyActivationStage::CleanupEnqueued;
    result.l1_cleanup_jobs = cleanup.l1_cleanup_jobs;
    result.l2_cleanup_jobs = cleanup.l2_cleanup_jobs;
    result.cleanup_pending = cleanup.l2_cleanup_jobs > 0;

    record_completed_activation_stage(config_, PolicyActivationStage::CleanupEnqueued,
                                      &stage_clock);
    result.stage = PolicyActivationStage::MaterializedVariantsInvalidated;
    const std::uint32_t materialized_invalidations =
        count_materialized_variant_invalidations(cleanup.epoch_res);

    std::shared_ptr<const runtime::RuntimePolicySnapshot> snapshot_to_swap;
    if (request.committed_snapshot != nullptr) {
        record_completed_activation_stage(
            config_, PolicyActivationStage::MaterializedVariantsInvalidated, &stage_clock);
        snapshot_to_swap = request.committed_snapshot;
        result.stage = PolicyActivationStage::SnapshotBuilt;
    } else {
        const runtime::RuntimePolicySnapshotBuildResult build =
            config_.snapshot_build_fn != nullptr
                ? config_.snapshot_build_fn(after_ir, request.generation)
                : runtime::build_runtime_policy_snapshot_from_ir(after_ir, request.generation);
        if (!build.ok || build.snapshot == nullptr) {
            rollback_route_epochs_for_apply(config_.route_cache_epoch_store, epoch_res);
            return fail_activation(std::move(result), PolicyActivationStage::SnapshotBuilt,
                                   "POLICY_ACTIVATION_SNAPSHOT_BUILD_FAILED", build.error, true,
                                   config_, request, activation_start, stage_clock);
        }
        snapshot_to_swap = build.snapshot;
        record_completed_activation_stage(
            config_, PolicyActivationStage::MaterializedVariantsInvalidated, &stage_clock);
        result.stage = PolicyActivationStage::SnapshotBuilt;
    }

    const auto current_snapshot = config_.runtime_policy_store->load();
    const std::string expected_identity =
        current_snapshot != nullptr ? current_snapshot->policy_identity : std::string{};

    std::string swap_err;
    const bool swapped = expected_identity.empty()
                             ? config_.runtime_policy_store->swap(snapshot_to_swap, &swap_err)
                             : config_.runtime_policy_store->swap_if_current(
                                   expected_identity, snapshot_to_swap, &swap_err);
    if (!swapped) {
        rollback_route_epochs_for_apply(config_.route_cache_epoch_store, epoch_res);
        return fail_activation(std::move(result), PolicyActivationStage::SnapshotSwapped,
                               "POLICY_ACTIVATION_SNAPSHOT_SWAP_FAILED",
                               swap_err.empty() ? "snapshot swap failed" : swap_err, true, config_,
                               request, activation_start, stage_clock);
    }
    record_completed_activation_stage(config_, PolicyActivationStage::SnapshotBuilt, &stage_clock);
    result.stage = PolicyActivationStage::SnapshotSwapped;
    result.old_snapshot_still_active = false;

    record_completed_activation_stage(config_, PolicyActivationStage::SnapshotSwapped,
                                      &stage_clock);
    result.stage = PolicyActivationStage::ActivationPublished;
    result.ok = true;
    record_completed_activation_stage(config_, PolicyActivationStage::ActivationPublished,
                                      &stage_clock);
    result.stage = PolicyActivationStage::Activated;
    result.status = result.cleanup_pending ? PolicyActivationStatus::PolicyActiveCleanupPending
                                           : PolicyActivationStatus::PolicyActive;
    result.message = result.cleanup_pending
                         ? "Policy activated; background cache cleanup is pending."
                         : "Policy activated.";
    const auto duration_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - activation_start)
                                       .count());
    if (config_.runtime_policy_metrics != nullptr) {
        runtime_policy::record_activation_operational_counts(
            config_.runtime_policy_metrics, result.bumped_route_epochs, result.l1_cleanup_jobs,
            result.l2_cleanup_jobs, materialized_invalidations, result.cleanup_pending);
    }
    emit_activation_event(
        config_, request, control_plane::PolicyLifecycleEventType::PolicyActivationSucceeded,
        "success", "activated", "", result.message, duration_ms, false, result.cleanup_pending);
    return result;
}

} // namespace bytetaper::operational

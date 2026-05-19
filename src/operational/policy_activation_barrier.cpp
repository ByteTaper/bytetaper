// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/policy_activation_barrier.h"

#include "operational/cache_namespace_sync.h"
#include "operational/materialized_variant_sync.h"
#include "operational/policy_cleanup_sync.h"
#include "operational/policy_operational_diff.h"
#include "operational/route_epoch_sync.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"

namespace bytetaper::operational {

namespace {

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

PolicyActivationBarrier::PolicyActivationBarrier(PolicyActivationBarrierConfig config)
    : config_(std::move(config)) {}

PolicyActivationResult PolicyActivationBarrier::activate(const PolicyActivationRequest& request) {
    PolicyActivationResult result{};
    result.resource_key = config_.resource_key.to_string();
    result.generation = request.generation;
    result.policy_id = request.policy_id;
    result.status = PolicyActivationStatus::PolicyActivationInProgress;
    result.stage = PolicyActivationStage::Committed;

    if (config_.policy_state_store == nullptr || config_.runtime_policy_store == nullptr) {
        return make_failure(std::move(result), PolicyActivationStage::Committed,
                            "POLICY_ACTIVATION_STORE_UNAVAILABLE",
                            "policy state store or runtime policy store is not configured", true);
    }

    taperquery::TqPolicyDocument before_ir{};
    taperquery::TqPolicyDocument after_ir{};

    const std::uint64_t before_generation =
        request.previous_generation > 0 ? request.previous_generation : request.generation - 1;

    if (before_generation > 0) {
        std::string load_err;
        if (!load_policy_ir_from_store(config_.policy_state_store, config_.resource_key,
                                       before_generation, &before_ir, &load_err)) {
            return make_failure(std::move(result), PolicyActivationStage::Committed,
                                "POLICY_ACTIVATION_LOAD_BEFORE_FAILED",
                                "failed to load previous policy version: " + load_err, true);
        }
    }

    if (request.committed_policy_ir != nullptr) {
        after_ir = *request.committed_policy_ir;
        const std::string computed_policy_id =
            taperquery::compute_policy_document_identity(after_ir);
        if (computed_policy_id != request.policy_id) {
            return make_failure(std::move(result), PolicyActivationStage::Committed,
                                "POLICY_ACTIVATION_POLICY_ID_MISMATCH",
                                "committed policy identity does not match request policy_id", true);
        }
    } else {
        std::string after_load_err;
        if (!load_policy_ir_from_store(config_.policy_state_store, config_.resource_key,
                                       request.generation, &after_ir, &after_load_err)) {
            return make_failure(std::move(result), PolicyActivationStage::Committed,
                                "POLICY_ACTIVATION_LOAD_COMMITTED_FAILED",
                                "failed to load committed policy version: " + after_load_err, true);
        }
    }

    taperquery::TqApplyPlanOptions plan_opts{};
    plan_opts.include_field_level_changes = true;
    const PolicyOperationalDiffResult diff =
        compute_policy_operational_diff(before_ir, after_ir, plan_opts);
    result.stage = PolicyActivationStage::OperationalDiffComputed;
    result.affected_routes = diff.affected_routes;

    if (config_.route_cache_epoch_store != nullptr) {
        const auto impact =
            detect_cache_namespace_impacts_for_plan(diff.plan, config_.route_cache_epoch_store);
        if (!impact.ok) {
            return make_failure(std::move(result), PolicyActivationStage::OperationalDiffComputed,
                                "POLICY_ACTIVATION_CACHE_NAMESPACE_FAILED", impact.error, true);
        }
    }
    result.stage = PolicyActivationStage::CacheNamespacePrepared;

    taperquery::TqCacheNamespaceApplyResult epoch_res;
    if (config_.route_cache_epoch_store != nullptr) {
        epoch_res = sync_route_epochs_for_apply(diff.plan, config_.route_cache_epoch_store);
        if (!epoch_res.ok) {
            return make_failure(std::move(result), PolicyActivationStage::RouteEpochsBumped,
                                "POLICY_ACTIVATION_EPOCH_BUMP_FAILED", epoch_res.error, true);
        }
    } else {
        epoch_res.ok = true;
    }
    result.stage = PolicyActivationStage::RouteEpochsBumped;
    result.bumped_route_epochs = count_bumped_route_epochs(epoch_res);

    const PolicyCleanupSyncResult cleanup =
        ::bytetaper::operational::prepare_operational_sync_for_apply(epoch_res, config_.l1_cache,
                                                                     config_.l2_cleanup_queue);
    if (!cleanup.epoch_res.ok) {
        rollback_route_epochs_for_apply(config_.route_cache_epoch_store, epoch_res);
        return make_failure(std::move(result), PolicyActivationStage::CleanupEnqueued,
                            "POLICY_ACTIVATION_CLEANUP_FAILED", cleanup.epoch_res.error, true);
    }
    result.stage = PolicyActivationStage::CleanupEnqueued;
    result.l1_cleanup_jobs = cleanup.l1_cleanup_jobs;
    result.l2_cleanup_jobs = cleanup.l2_cleanup_jobs;
    result.cleanup_pending = cleanup.l2_cleanup_jobs > 0;

    result.stage = PolicyActivationStage::MaterializedVariantsInvalidated;
    (void) count_materialized_variant_invalidations(cleanup.epoch_res);

    std::shared_ptr<const runtime::RuntimePolicySnapshot> snapshot_to_swap;
    if (request.committed_snapshot != nullptr) {
        snapshot_to_swap = request.committed_snapshot;
        result.stage = PolicyActivationStage::SnapshotBuilt;
    } else {
        const runtime::RuntimePolicySnapshotBuildResult build =
            config_.snapshot_build_fn != nullptr
                ? config_.snapshot_build_fn(after_ir, request.generation)
                : runtime::build_runtime_policy_snapshot_from_ir(after_ir, request.generation);
        if (!build.ok || build.snapshot == nullptr) {
            rollback_route_epochs_for_apply(config_.route_cache_epoch_store, epoch_res);
            return make_failure(std::move(result), PolicyActivationStage::SnapshotBuilt,
                                "POLICY_ACTIVATION_SNAPSHOT_BUILD_FAILED", build.error, true);
        }
        snapshot_to_swap = build.snapshot;
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
        return make_failure(std::move(result), PolicyActivationStage::SnapshotSwapped,
                            "POLICY_ACTIVATION_SNAPSHOT_SWAP_FAILED",
                            swap_err.empty() ? "snapshot swap failed" : swap_err, true);
    }
    result.stage = PolicyActivationStage::SnapshotSwapped;
    result.old_snapshot_still_active = false;

    result.stage = PolicyActivationStage::ActivationPublished;
    result.ok = true;
    result.stage = PolicyActivationStage::Activated;
    result.status = result.cleanup_pending ? PolicyActivationStatus::PolicyActiveCleanupPending
                                           : PolicyActivationStatus::PolicyActive;
    result.message = result.cleanup_pending
                         ? "Policy activated; background cache cleanup is pending."
                         : "Policy activated.";
    return result;
}

} // namespace bytetaper::operational

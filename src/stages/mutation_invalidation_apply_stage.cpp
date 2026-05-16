// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/mutation_invalidation_apply_stage.h"

#include "apg/context.h"
#include "metrics/cache_metrics.h"
#include "runtime/route_cache_epoch_store.h"

namespace bytetaper::stages {

apg::StageOutput mutation_invalidation_apply_stage(apg::ApgTransformContext& context) {
    auto& plan = context.mutation_invalidation;

    if (!plan.prepared) {
        return { apg::StageResult::Continue, "no-plan" };
    }

    if (plan.applied || plan.skipped || plan.failed) {
        return { apg::StageResult::Continue, "already-final" }; // idempotent
    }

    bytetaper::metrics::record_cache_event(
        context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationApplyAttempt);

    // Check status code
    if (context.response_status_code < plan.success_status_min ||
        context.response_status_code > plan.success_status_max) {
        plan.skipped = true;
        plan.decision = apg::MutationInvalidationDecision::SkippedStatusNotSuccessful;
        plan.reason = "status-not-successful";

        bytetaper::metrics::record_cache_event(
            context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationSkipped);
        bytetaper::metrics::record_cache_event(
            context.cache_metrics,
            bytetaper::metrics::CacheMetricEvent::InvalidationStatusNotSuccessful);

        return { apg::StageResult::Continue, "status-not-successful" };
    }

    if (context.route_cache_epoch_store == nullptr) {
        plan.failed = true;
        plan.decision = apg::MutationInvalidationDecision::FailedEpochStoreMissing;
        plan.reason = "epoch-store-missing";

        bytetaper::metrics::record_cache_event(
            context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationFailed);

        return { apg::StageResult::Continue, "epoch-store-missing" };
    }

    // Execute bumps
    for (std::size_t i = 0; i < plan.target_count; ++i) {
        bytetaper::metrics::record_cache_event(
            context.cache_metrics, bytetaper::metrics::CacheMetricEvent::RouteEpochBumpAttempt);

        std::uint64_t new_epoch = 0;
        const auto res = runtime::route_cache_epoch_bump(context.route_cache_epoch_store,
                                                         plan.targets[i].route_id, &new_epoch);
        if (res != runtime::RouteCacheEpochResult::Ok) {
            plan.failed = true;
            plan.decision = apg::MutationInvalidationDecision::FailedEpochBump;
            plan.reason = "epoch-bump-failed";

            bytetaper::metrics::record_cache_event(
                context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationFailed);
            bytetaper::metrics::record_cache_event(
                context.cache_metrics, bytetaper::metrics::CacheMetricEvent::RouteEpochBumpFailed);

            return { apg::StageResult::Continue, "epoch-bump-failed" };
        }

        bytetaper::metrics::record_cache_event(
            context.cache_metrics, bytetaper::metrics::CacheMetricEvent::RouteEpochBumpSuccess);

        // Record variant invalidation if target route has variants enabled
        for (std::size_t j = 0; j < context.active_route_count; ++j) {
            if (context.active_routes[j].route_id != nullptr &&
                std::strcmp(context.active_routes[j].route_id, plan.targets[i].route_id) == 0) {
                if (context.active_routes[j].cache.field_variant.enabled) {
                    bytetaper::metrics::record_cache_event(
                        context.cache_metrics,
                        bytetaper::metrics::CacheMetricEvent::VariantInvalidatedByRouteEpoch);
                }
                break;
            }
        }
    }

    plan.applied = true;
    plan.decision = apg::MutationInvalidationDecision::Applied;
    plan.reason = "epoch-bumped";

    bytetaper::metrics::record_cache_event(
        context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationApplied);

    return { apg::StageResult::Continue, "epoch-bumped" };
}

} // namespace bytetaper::stages

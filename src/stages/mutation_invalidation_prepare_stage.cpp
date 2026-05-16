// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/mutation_invalidation_prepare_stage.h"

#include "apg/context.h"
#include "cache/cache_invalidation_target_resolver.h"
#include "metrics/cache_metrics.h"
#include "policy/route_policy.h"

#include <cstring>

namespace bytetaper::stages {

apg::StageOutput mutation_invalidation_prepare_stage(apg::ApgTransformContext& context) {
    auto& plan = context.mutation_invalidation;
    plan = apg::PendingMutationInvalidationPlan{};

    if (context.matched_policy == nullptr) {
        bytetaper::metrics::record_cache_event(
            context.cache_metrics,
            bytetaper::metrics::CacheMetricEvent::InvalidationPrepareSkipped);
        bytetaper::metrics::record_cache_event(
            context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationNoPolicy);
        plan.decision = apg::MutationInvalidationDecision::SkippedNoPolicy;
        plan.reason = "no-policy";
        return { apg::StageResult::Continue, "no-policy" };
    }

    const bool has_invalidation_policy = context.matched_policy->cache.invalidation.enabled;

    if (has_invalidation_policy) {
        bytetaper::metrics::record_cache_event(
            context.cache_metrics,
            bytetaper::metrics::CacheMetricEvent::InvalidationPrepareAttempt);
    }

    auto resolved =
        cache::resolve_invalidation_targets(context.active_routes, context.active_route_count,
                                            context.matched_policy, context.request_method);

    if (!resolved.ok) {
        bytetaper::metrics::record_cache_event(
            context.cache_metrics,
            bytetaper::metrics::CacheMetricEvent::InvalidationPrepareSkipped);

        if (resolved.status == cache::InvalidationTargetResolveStatus::NonMutationMethod) {
            plan.decision = apg::MutationInvalidationDecision::SkippedNonMutationMethod;
        } else if (resolved.status == cache::InvalidationTargetResolveStatus::MethodNotEnabled) {
            plan.decision = apg::MutationInvalidationDecision::SkippedMethodNotEnabled;
            bytetaper::metrics::record_cache_event(
                context.cache_metrics,
                bytetaper::metrics::CacheMetricEvent::InvalidationMethodNotEnabled);
        } else if (resolved.status == cache::InvalidationTargetResolveStatus::NoPolicy ||
                   resolved.status == cache::InvalidationTargetResolveStatus::Disabled) {
            plan.decision = apg::MutationInvalidationDecision::SkippedNoPolicy;
            bytetaper::metrics::record_cache_event(
                context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationNoPolicy);
        } else {
            plan.decision = apg::MutationInvalidationDecision::SkippedNoPolicy;
            bytetaper::metrics::record_cache_event(
                context.cache_metrics,
                bytetaper::metrics::CacheMetricEvent::InvalidationTargetResolveFailed);
        }
        plan.reason = resolved.reason;
        return { apg::StageResult::Continue, resolved.reason };
    }

    // Prepare the plan
    plan.prepared = true;
    plan.decision = apg::MutationInvalidationDecision::Prepared;
    plan.success_status_min = resolved.success_status_min;
    plan.success_status_max = resolved.success_status_max;

    for (std::size_t i = 0; i < resolved.target_count; ++i) {
        const auto& src = resolved.targets[i];
        auto& dst = plan.targets[i];
        std::strncpy(dst.route_id, src.route_id, apg::kInvalidationRouteIdMaxLen - 1);
        dst.route_id[apg::kInvalidationRouteIdMaxLen - 1] = '\0';
        plan.target_count++;
    }

    bytetaper::metrics::record_cache_event(
        context.cache_metrics, bytetaper::metrics::CacheMetricEvent::InvalidationPrepared);
    plan.reason = "prepared";
    return { apg::StageResult::Continue, "prepared" };
}

} // namespace bytetaper::stages

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/mutation_invalidation_prepare_stage.h"

#include "apg/context.h"
#include "policy/route_policy.h"

#include <cstring>

namespace bytetaper::stages {

apg::StageOutput mutation_invalidation_prepare_stage(apg::ApgTransformContext& context) {
    auto& plan = context.mutation_invalidation;
    plan = apg::PendingMutationInvalidationPlan{};

    const policy::RoutePolicy* pol = context.matched_policy;
    if (pol == nullptr) {
        plan.decision = apg::MutationInvalidationDecision::SkippedNoPolicy;
        return { apg::StageResult::Continue, "no-policy" };
    }

    if (!pol->cache.invalidation.enabled) {
        plan.decision = apg::MutationInvalidationDecision::SkippedNoPolicy;
        return { apg::StageResult::Continue, "no-invalidation-policy" };
    }

    const policy::HttpMethod method = context.request_method;
    bool method_supported = false;
    bool method_enabled = false;

    if (method == policy::HttpMethod::Put) {
        method_supported = true;
        method_enabled = pol->cache.invalidation.on_put;
    } else if (method == policy::HttpMethod::Patch) {
        method_supported = true;
        method_enabled = pol->cache.invalidation.on_patch;
    } else if (method == policy::HttpMethod::Delete) {
        method_supported = true;
        method_enabled = pol->cache.invalidation.on_delete;
    }

    if (!method_supported) {
        plan.decision = apg::MutationInvalidationDecision::SkippedNonMutationMethod;
        return { apg::StageResult::Continue, "non-mutation-method" };
    }

    if (!method_enabled) {
        plan.decision = apg::MutationInvalidationDecision::SkippedMethodNotEnabled;
        return { apg::StageResult::Continue, "method-not-enabled" };
    }

    // Prepare the plan
    plan.prepared = true;
    plan.decision = apg::MutationInvalidationDecision::Prepared;
    plan.success_status_min = pol->cache.invalidation.success_status_min;
    plan.success_status_max = pol->cache.invalidation.success_status_max;

    const std::size_t limit =
        (pol->cache.invalidation.target_count < apg::kMaxPendingInvalidationTargets)
            ? pol->cache.invalidation.target_count
            : apg::kMaxPendingInvalidationTargets;

    for (std::size_t i = 0; i < limit; ++i) {
        const auto& src_target = pol->cache.invalidation.targets[i];
        auto& dst_target = plan.targets[i];
        std::strncpy(dst_target.route_id, src_target.route_id, apg::kInvalidationRouteIdMaxLen - 1);
        dst_target.route_id[apg::kInvalidationRouteIdMaxLen - 1] = '\0';
        plan.target_count++;
    }

    plan.reason = "prepared";
    return { apg::StageResult::Continue, "prepared" };
}

} // namespace bytetaper::stages

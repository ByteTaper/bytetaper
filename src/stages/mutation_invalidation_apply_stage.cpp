// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/mutation_invalidation_apply_stage.h"

#include "apg/context.h"
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

    // Check status code
    if (context.response_status_code < plan.success_status_min ||
        context.response_status_code > plan.success_status_max) {
        plan.skipped = true;
        plan.decision = apg::MutationInvalidationDecision::SkippedStatusNotSuccessful;
        plan.reason = "status-not-successful";
        return { apg::StageResult::Continue, "status-not-successful" };
    }

    if (context.route_cache_epoch_store == nullptr) {
        plan.failed = true;
        plan.decision = apg::MutationInvalidationDecision::FailedEpochStoreMissing;
        plan.reason = "epoch-store-missing";
        return { apg::StageResult::Continue, "epoch-store-missing" };
    }

    // Execute bumps
    for (std::size_t i = 0; i < plan.target_count; ++i) {
        std::uint64_t new_epoch = 0;
        const auto res = runtime::route_cache_epoch_bump(context.route_cache_epoch_store,
                                                         plan.targets[i].route_id, &new_epoch);
        if (res != runtime::RouteCacheEpochResult::Ok) {
            plan.failed = true;
            plan.decision = apg::MutationInvalidationDecision::FailedEpochBump;
            plan.reason = "epoch-bump-failed";
            return { apg::StageResult::Continue, "epoch-bump-failed" };
        }
    }

    plan.applied = true;
    plan.decision = apg::MutationInvalidationDecision::Applied;
    plan.reason = "epoch-bumped";

    return { apg::StageResult::Continue, "epoch-bumped" };
}

} // namespace bytetaper::stages

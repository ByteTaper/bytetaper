// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/coalescing_decision_stage.h"

#include "coalescing/coalescing_decision.h"
#include "coalescing/coalescing_eligibility.h"
#include "coalescing/coalescing_key.h"
#include "coalescing/coalescing_safety.h"
#include "coalescing/inflight_registry.h"

#include <cstdio>

namespace bytetaper::stages {

apg::StageOutput coalescing_decision_stage(apg::ApgTransformContext& context) {
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue };
    }

    const auto& policy = context.matched_policy->coalescing;
    if (!policy.enabled) {
        return { apg::StageResult::Continue };
    }

    coalescing::CoalescingDecisionContext decision_ctx{};
    decision_ctx.policy = &policy;
    decision_ctx.method = context.request_method;
    decision_ctx.metrics = context.coalescing_metrics;
    decision_ctx.now_ms = static_cast<std::uint64_t>(context.request_epoch_ms);

    decision_ctx.key_input.route_name = context.matched_policy->route_id;
    decision_ctx.key_input.normalized_path = context.raw_path;
    decision_ctx.key_input.normalized_query = context.raw_query;

    // Safety input: we don't have cookies/headers easily here, but we can check if it is
    // authenticated via a bypass if allow_authenticated is false.
    decision_ctx.safety_input.allow_authenticated = policy.allow_authenticated;
    decision_ctx.safety_input.has_authorization_header =
        false;                                           // TODO: populate from headers if available
    decision_ctx.safety_input.has_cookie_header = false; // TODO: populate from headers if available

    context.coalescing_decision =
        coalescing::evaluate_coalescing_decision(context.coalescing_registry, decision_ctx);

    if (context.coalescing_decision.action == coalescing::CoalescingAction::Reject) {
        return { apg::StageResult::Error, "too-many-waiters" };
    }

    return { apg::StageResult::Continue };
}

} // namespace bytetaper::stages

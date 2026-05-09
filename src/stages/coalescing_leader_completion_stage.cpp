// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/coalescing_leader_completion_stage.h"

#include "coalescing/coalescing_completion_handoff.h"
#include "coalescing/inflight_registry.h"
#include "metrics/coalescing_metrics.h"
#include "policy/route_policy.h"

#include <chrono>

namespace bytetaper::stages {

apg::StageOutput coalescing_leader_completion_stage(apg::ApgTransformContext& context) {
    // Only leaders need to signal completion
    if (context.coalescing_decision.action != coalescing::CoalescingAction::Leader) {
        return { apg::StageResult::Continue, "not-leader" };
    }

    if (context.coalescing_registry == nullptr) {
        return { apg::StageResult::Continue, "no-registry" };
    }

    // Capture completion time
    auto now = std::chrono::system_clock::now();
    std::uint64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    const bool response_valid = context.cache_key_ready && context.response_status_code >= 200 &&
                                context.response_status_code < 300 &&
                                context.response_body != nullptr && context.response_body_len > 0;

    if (!response_valid) {
        // non-2xx or no body
        coalescing::registry_complete_state_if_generation(
            context.coalescing_registry, context.coalescing_decision.key,
            context.coalescing_decision.lifecycle_generation,
            context.response_status_code >= 200 && context.response_status_code < 300
                ? coalescing::InFlightCompletionState::NotCacheable
                : coalescing::InFlightCompletionState::Failed,
            now_ms);
        return { apg::StageResult::Continue, "completed-cleared" };
    }

    switch (coalescing::decide_coalescing_completion_handoff(context.response_body_len)) {
    case coalescing::CoalescingCompletionHandoffTarget::L1Inline:
        coalescing::registry_complete_state_if_generation(
            context.coalescing_registry, context.coalescing_decision.key,
            context.coalescing_decision.lifecycle_generation,
            coalescing::InFlightCompletionState::L1Ready, now_ms);
        metrics::record_coalescing_event(context.coalescing_metrics,
                                         metrics::CoalescingMetricEvent::LeaderL1StoreSuccess);
        return { apg::StageResult::Continue, "completed-l1-ready" };

    case coalescing::CoalescingCompletionHandoffTarget::L2Completion:
        // Registry stays InFlight; L2 worker will publish L2Ready after l2_put().
        metrics::record_coalescing_event(context.coalescing_metrics,
                                         metrics::CoalescingMetricEvent::LeaderL2HandoffPending);
        return { apg::StageResult::Continue, "awaiting-l2-completion" };

    case coalescing::CoalescingCompletionHandoffTarget::NotCacheable:
        coalescing::registry_complete_state_if_generation(
            context.coalescing_registry, context.coalescing_decision.key,
            context.coalescing_decision.lifecycle_generation,
            coalescing::InFlightCompletionState::NotCacheable, now_ms);
        metrics::record_coalescing_event(context.coalescing_metrics,
                                         metrics::CoalescingMetricEvent::LeaderL1StoreFailed);
        return { apg::StageResult::Continue, "completed-not-cacheable" };
    }

    return { apg::StageResult::Continue, "completed-cleared" };
}

} // namespace bytetaper::stages

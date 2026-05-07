// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/coalescing_leader_completion_stage.h"

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

    // L1 is already stored by l1_cache_store_stage. Signal L1Ready to followers.
    const bool l1_committed = context.cache_key_ready && context.response_status_code >= 200 &&
                              context.response_status_code < 300 &&
                              context.response_body != nullptr && context.response_body_len > 0;

    if (l1_committed) {
        coalescing::registry_complete_state(context.coalescing_registry,
                                            context.coalescing_decision.key,
                                            coalescing::InFlightCompletionState::L1Ready, now_ms);
        metrics::record_coalescing_event(context.coalescing_metrics,
                                         metrics::CoalescingMetricEvent::LeaderL1StoreSuccess);
    } else if (context.response_status_code >= 200 && context.response_status_code < 300) {
        coalescing::registry_complete_state(
            context.coalescing_registry, context.coalescing_decision.key,
            coalescing::InFlightCompletionState::NotCacheable, now_ms);
        metrics::record_coalescing_event(context.coalescing_metrics,
                                         metrics::CoalescingMetricEvent::LeaderL1StoreFailed);
    } else {
        coalescing::registry_complete_state(context.coalescing_registry,
                                            context.coalescing_decision.key,
                                            coalescing::InFlightCompletionState::Failed, now_ms);
    }

    return { apg::StageResult::Continue,
             l1_committed ? "completed-l1-ready" : "completed-cleared" };
}

} // namespace bytetaper::stages

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

    const bool cacheable = context.matched_policy != nullptr &&
                           context.matched_policy->cache.behavior == policy::CacheBehavior::Store &&
                           context.response_status_code >= 200 &&
                           context.response_status_code < 300 && context.response_body != nullptr &&
                           context.response_body_len > 0 &&
                           context.response_body_len <= coalescing::kCoalescingSharedBodyMaxSize;

    // Capture completion time
    auto now = std::chrono::system_clock::now();
    std::uint64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    bool success = false;
    if (cacheable) {
        success = coalescing::registry_complete_with_response(
            context.coalescing_registry, context.coalescing_decision.key,
            context.response_status_code, context.response_content_type, context.response_body,
            context.response_body_len, now_ms);
    } else if (context.response_status_code >= 200 && context.response_status_code < 300) {
        success = coalescing::registry_complete_state(
            context.coalescing_registry, context.coalescing_decision.key,
            coalescing::CoalescingState::NotCacheable, now_ms);
    } else {
        success = coalescing::registry_complete_state(
            context.coalescing_registry, context.coalescing_decision.key,
            coalescing::CoalescingState::LeaderFailed, now_ms);
    }

    if (success) {
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::LeaderResultPublished);
    } else {
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::LeaderResultPublishFailed);
    }

    return { apg::StageResult::Continue, cacheable ? "completed-cacheable" : "completed-cleared" };
}

} // namespace bytetaper::stages

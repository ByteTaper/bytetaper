// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_COALESCING_COALESCING_DECISION_H
#define BYTETAPER_COALESCING_COALESCING_DECISION_H

#include "coalescing/coalescing_eligibility.h"
#include "coalescing/coalescing_key.h"
#include "coalescing/coalescing_safety.h"
#include "coalescing/inflight_registry.h"
#include "policy/coalescing_policy.h"
#include "policy/route_policy.h"

#include <string_view>

namespace bytetaper::metrics {
struct CoalescingMetrics;
}

namespace bytetaper::coalescing {

/**
 * @brief Action to be taken by the proxy for a request.
 */
enum class CoalescingAction : std::uint8_t {
    Leader = 0,   // Proceeds to backend, results cached and shared.
    Follower = 1, // Wait for leader's response.
    Bypass = 2,   // Coalescing disabled or ineligible; proceed independently.
    Reject = 3,   // Queue full; synthesize error response (429/503).
};

/**
 * @brief Detailed reason for the coalescing action.
 */
enum class CoalescingDecisionReason : std::uint8_t {
    PolicyDisabled = 0,
    MethodNotGet = 1,
    AuthenticatedRequest = 2,
    MissingKey = 3,
    LeaderCreated = 4,
    FollowerJoined = 5,
    TooManyWaiters = 6,
    WaitWindowExpired = 7,
};

/**
 * @brief Final decision result from the coalescing orchestrator.
 */
struct CoalescingDecision {
    CoalescingAction action;
    CoalescingDecisionReason reason;
    char key[256] = { 0 };
};

/**
 * @brief Input context for the coalescing decision orchestration.
 */
struct CoalescingDecisionContext {
    const policy::CoalescingPolicy* policy = nullptr;
    policy::HttpMethod method = policy::HttpMethod::Any;
    CoalescingSafetyInput safety_input;
    CoalescingKeyInput key_input;
    std::uint64_t now_ms = 0;
    metrics::CoalescingMetrics* metrics = nullptr;
};

/**
 * @brief Returns a stable string representation of the decision reason.
 */
std::string_view get_decision_reason_string(CoalescingDecisionReason reason);

/**
 * @brief Orchestrates the entire coalescing pipeline to reach a final decision.
 *
 * Sequentially evaluates policy, eligibility, safety, key generation, and registry state.
 *
 * @param registry The in-flight request registry.
 * @param context Input parameters for the decision.
 * @return CoalescingDecision The final decision and reason.
 */
CoalescingDecision evaluate_coalescing_decision(InFlightRegistry* registry,
                                                const CoalescingDecisionContext& context);

} // namespace bytetaper::coalescing

#endif // BYTETAPER_COALESCING_COALESCING_DECISION_H

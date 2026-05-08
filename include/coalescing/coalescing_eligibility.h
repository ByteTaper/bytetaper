// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_COALESCING_COALESCING_ELIGIBILITY_H
#define BYTETAPER_COALESCING_COALESCING_ELIGIBILITY_H

#include "policy/method_matcher.h"

#include <string_view>

namespace bytetaper::coalescing {

/**
 * @brief Enum representing reasons why a request might be ineligible for coalescing.
 */
enum class CoalescingRejectionReason {
    None = 0,
    MethodNotGet,
    AuthenticatedRequest,
};

/**
 * @brief Result of a coalescing eligibility check.
 */
struct CoalescingEligibility {
    bool is_eligible = false;
    CoalescingRejectionReason rejection_reason = CoalescingRejectionReason::None;
};

/**
 * @brief Returns a stable string representation of the rejection reason.
 *
 * @param reason The rejection reason.
 * @return std::string_view A string describing the reason.
 */
std::string_view get_rejection_reason_string(CoalescingRejectionReason reason);

/**
 * @brief Evaluates if a request is eligible for coalescing based on its HTTP method.
 *
 *
 * @param method The HTTP method of the request.
 * @return CoalescingEligibility The eligibility result.
 */
CoalescingEligibility evaluate_coalescing_eligibility(policy::HttpMethod method);

} // namespace bytetaper::coalescing

#endif // BYTETAPER_COALESCING_COALESCING_ELIGIBILITY_H

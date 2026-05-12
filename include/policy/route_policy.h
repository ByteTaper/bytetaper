// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_POLICY_ROUTE_POLICY_H
#define BYTETAPER_POLICY_ROUTE_POLICY_H

#include "policy/cache_policy.h"
#include "policy/coalescing_policy.h"
#include "policy/coalescing_policy_validator.h"
#include "policy/compression_policy.h"
#include "policy/field_filter_policy.h"
#include "policy/pagination_policy.h"

#include <cstdint>
#include <cstring>

namespace bytetaper::policy {

enum class RouteMatchKind : std::uint8_t {
    Prefix = 0,
    Exact = 1,
};

enum class MutationMode : std::uint8_t {
    Disabled = 0,
    HeadersOnly = 1,
    Full = 2,
};

enum class FailureMode : std::uint8_t {
    FailOpen = 0,
    FailClosed = 1,
};

enum class HttpMethod : std::uint8_t {
    Any = 0,
    Get = 1,
    Post = 2,
    Put = 3,
    Delete = 4,
    Patch = 5,
};

static constexpr std::size_t kPolicyIdentityMaxLen = 64;

struct RoutePolicy {
    const char* route_id = nullptr;
    char policy_identity[kPolicyIdentityMaxLen] = {};
    const char* match_prefix = nullptr;
    RouteMatchKind match_kind = RouteMatchKind::Prefix;
    MutationMode mutation = MutationMode::Disabled;
    HttpMethod allowed_method = HttpMethod::Any;
    FieldFilterPolicy field_filter = {};
    std::uint32_t max_response_bytes = 0;
    CachePolicy cache = {};
    FailureMode failure_mode = FailureMode::FailOpen;
    PaginationPolicy pagination = {};
    CompressionPolicy compression = {};
    CoalescingPolicy coalescing = {};
};

bool validate_route_policy(const RoutePolicy& policy, const char** reason_out);

// Returns true if the given response_size (in bytes) exceeds the policy limit.
// If policy.max_response_bytes is 0, always returns false (no limit).
inline bool exceeds_max_response_bytes(const RoutePolicy& policy, std::uint32_t response_size) {
    if (policy.max_response_bytes == 0) {
        return false;
    }
    return response_size > policy.max_response_bytes;
}

} // namespace bytetaper::policy

#endif // BYTETAPER_POLICY_ROUTE_POLICY_H

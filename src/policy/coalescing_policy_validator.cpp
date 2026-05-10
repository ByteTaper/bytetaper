// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/coalescing_policy_validator.h"

#include "policy/cache_policy.h"

#include <cstdio>

namespace bytetaper::policy {

const char* validate_coalescing_policy_safe(const CoalescingPolicy& policy,
                                            const CachePolicy* cache_policy) {
    // 1. Delegate structural checks first
    const char* base_err = validate_coalescing_policy(policy);
    if (base_err != nullptr) {
        return base_err;
    }

    if (!policy.enabled) {
        return nullptr;
    }

    // 2. Budget sanity check
    if (policy.backend_timeout_ms + policy.handoff_buffer_ms > 30000) {
        return "coalescing total wait budget exceeds 30000ms";
    }
    if (policy.handoff_buffer_ms > policy.backend_timeout_ms) {
        return "coalescing handoff_buffer_ms exceeds backend_timeout_ms";
    }
    if (policy.max_follower_wait_budget_ms > 0 &&
        policy.max_follower_wait_budget_ms < policy.handoff_buffer_ms) {
        return "coalescing max_follower_wait_budget_ms must be >= handoff_buffer_ms";
    }
    if (policy.max_active_follower_waiters > 0 &&
        policy.max_active_follower_waiters_per_shard > policy.max_active_follower_waiters) {
        return "coalescing max_active_follower_waiters_per_shard must be <= "
               "max_active_follower_waiters";
    }

    std::fprintf(stderr, "  coalescing.follower_wait_budget_ms = %u + %u = %u\n",
                 policy.backend_timeout_ms, policy.handoff_buffer_ms,
                 policy.backend_timeout_ms + policy.handoff_buffer_ms);

    // 3. Unsupported mode check (future-proofing)
    if (policy.mode != CoalescingMode::CacheAssisted) {
        return "unsupported coalescing mode";
    }

    // 4. Missing cache dependency check
    if (policy.require_cache_enabled) {
        if (cache_policy == nullptr || !cache_policy->enabled) {
            return "coalescing requires cache to be enabled";
        }
    }

    // 5. Auth without scope check
    if (policy.allow_authenticated) {
        if (cache_policy == nullptr || cache_policy->auth_scope_header[0] == '\0') {
            return "coalescing.allow_authenticated=true requires cache.auth_scope_header to be set";
        }
    }

    return nullptr;
}

} // namespace bytetaper::policy

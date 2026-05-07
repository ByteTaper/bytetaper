// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_POLICY_COALESCING_POLICY_H
#define BYTETAPER_POLICY_COALESCING_POLICY_H

#include <cstdint>

namespace bytetaper::policy {

enum class CoalescingMode : std::uint8_t {
    CacheAssisted = 0,
};

struct CoalescingPolicy {
    bool enabled = false;
    CoalescingMode mode = CoalescingMode::CacheAssisted;

    // Budget-aware follower wait policy (BT-037H-03)
    std::uint32_t backend_timeout_ms = 500;       // expected max leader upstream latency
    std::uint32_t handoff_buffer_ms = 250;        // extra time for notify + wakeup path
    std::uint32_t result_ready_retention_ms = 50; // InFlightEntry result retention (not cache TTL)

    std::uint32_t max_waiters_per_key = 64;
    bool require_cache_enabled = true;
    bool allow_authenticated = false;
};

struct CachePolicy;

// Validates the coalescing policy. Returns nullptr on success, or a static error string.
const char* validate_coalescing_policy(const CoalescingPolicy& policy);

} // namespace bytetaper::policy

#endif // BYTETAPER_POLICY_COALESCING_POLICY_H

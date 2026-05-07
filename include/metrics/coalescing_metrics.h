// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bytetaper::metrics {

enum class CoalescingMetricEvent : std::uint8_t {
    Leader,
    Follower,
    FollowerCacheHit,
    Fallback,
    Bypass,
    TooManyWaiters,
    LeaderResultPublished,
    LeaderResultPublishFailed,
    FollowerServedFromResult,
    FollowerTimeout,
    FollowerFallback,
    EntryCleanup
};

enum class UpstreamCallReason : std::uint8_t {
    LeaderFill = 0,
    FollowerTimeoutFallback = 1,
    Bypass = 2,
    CoalescingDisabled = 3,
    ErrorRecovery = 4,
    Unknown = 5,
};

struct CoalescingMetrics {
    std::atomic<std::uint64_t> leader_total{ 0 };
    std::atomic<std::uint64_t> follower_total{ 0 };
    std::atomic<std::uint64_t> follower_cache_hit_total{ 0 };
    std::atomic<std::uint64_t> fallback_total{ 0 };
    std::atomic<std::uint64_t> bypass_total{ 0 };
    std::atomic<std::uint64_t> too_many_waiters_total{ 0 };

    std::atomic<std::uint64_t> leader_result_published_total{ 0 };
    std::atomic<std::uint64_t> leader_result_publish_failed_total{ 0 };
    std::atomic<std::uint64_t> follower_served_from_result_total{ 0 };
    std::atomic<std::uint64_t> follower_timeout_total{ 0 };
    std::atomic<std::uint64_t> follower_fallback_total{ 0 };
    std::atomic<std::uint64_t> entry_cleanup_total{ 0 };

    // Upstream call reasons
    std::atomic<std::uint64_t> upstream_call_reason_leader_fill_total{ 0 };
    std::atomic<std::uint64_t> upstream_call_reason_follower_timeout_total{ 0 };
    std::atomic<std::uint64_t> upstream_call_reason_bypass_total{ 0 };
    std::atomic<std::uint64_t> upstream_call_reason_coalescing_disabled_total{ 0 };
    std::atomic<std::uint64_t> upstream_call_reason_error_recovery_total{ 0 };
    std::atomic<std::uint64_t> upstream_call_reason_unknown_total{ 0 };
};

void record_coalescing_event(CoalescingMetrics* metrics, CoalescingMetricEvent event);
void record_upstream_call_reason(CoalescingMetrics* metrics, UpstreamCallReason reason);

// Renders all counters as Prometheus text into buf.
// Returns bytes written (excluding null terminator), or 0 on overflow / null buf.
std::size_t render_coalescing_metrics_prometheus(const CoalescingMetrics& metrics, char* buf,
                                                 std::size_t buf_size);

} // namespace bytetaper::metrics

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
    FollowerTimeoutBeforePublish, // leader hadn't published when follower timed out (valid)
    FollowerTimeoutAfterPublish,  // leader had published but follower still timed out (bug)
    FallbackDeadlineUnknown,      // follower fell back; no client deadline available to check
    // Granular follower outcomes
    FollowerSharedResponse,
    FollowerL1Hit,
    FollowerTimeout,
    FollowerMissing,
    FollowerStoredButNoSnapshot,
    FollowerNotCacheable,
    FollowerFailed,
    FollowerUnaccounted,
    LeaderL1StoreSuccess,   // leader committed to L1 and signaled L1Ready
    LeaderL1StoreFailed,    // leader could not commit to L1 (non-cacheable or no cache_key)
    FollowerL1Ready,        // follower woke because of L1Ready signal
    FollowerL1ReadyButMiss, // L1Ready wakeup but L1 lookup still missed
    FollowerExpired,        // registered follower, entry replaced/recycled before completion
    LeaderL2HandoffPending,
    LeaderL2HandoffReady,
    LeaderL2HandoffFailed,
    FollowerL2Ready,
    FollowerL2Hit,
    FollowerL2ReadyButMiss,
    LeaderTooLargeForHandoff,      // leader body > 64 KiB; L2Ready not published
    FollowerTooLargeForHandoff,    // follower woke with TooLargeForHandoff result
    FollowerTimeoutL2BodyTooLarge, // final probe found L2 entry but body too large
};

struct CoalescingMetrics {
    std::atomic<std::uint64_t> leader_total{ 0 };
    std::atomic<std::uint64_t> follower_total{ 0 };
    std::atomic<std::uint64_t> follower_cache_hit_total{ 0 };
    std::atomic<std::uint64_t> fallback_total{ 0 };
    std::atomic<std::uint64_t> bypass_total{ 0 };
    std::atomic<std::uint64_t> too_many_waiters_total{ 0 };
    std::atomic<std::uint64_t> follower_timeout_before_publish_total{ 0 };
    std::atomic<std::uint64_t> follower_timeout_after_publish_total{ 0 };
    std::atomic<std::uint64_t> fallback_deadline_unknown_total{ 0 };
    // Granular follower outcome counters
    std::atomic<std::uint64_t> follower_shared_response_total{ 0 };
    std::atomic<std::uint64_t> follower_l1_hit_total{ 0 };
    std::atomic<std::uint64_t> follower_timeout_total{ 0 };
    std::atomic<std::uint64_t> follower_missing_total{ 0 };
    std::atomic<std::uint64_t> follower_stored_but_no_snapshot_total{ 0 };
    std::atomic<std::uint64_t> follower_not_cacheable_total{ 0 };
    std::atomic<std::uint64_t> follower_failed_total{ 0 };
    std::atomic<std::uint64_t> follower_unaccounted_total{ 0 };
    std::atomic<std::uint64_t> leader_l1_store_success_total{ 0 };
    std::atomic<std::uint64_t> leader_l1_store_failed_total{ 0 };
    std::atomic<std::uint64_t> follower_l1_ready_total{ 0 };
    std::atomic<std::uint64_t> follower_l1_ready_but_miss_total{ 0 };
    std::atomic<std::uint64_t> follower_expired_total{ 0 };
    std::atomic<std::uint64_t> leader_l2_handoff_pending_total{ 0 };
    std::atomic<std::uint64_t> leader_l2_handoff_ready_total{ 0 };
    std::atomic<std::uint64_t> leader_l2_handoff_failed_total{ 0 };
    std::atomic<std::uint64_t> follower_l2_ready_total{ 0 };
    std::atomic<std::uint64_t> follower_l2_hit_total{ 0 };
    std::atomic<std::uint64_t> follower_l2_ready_but_miss_total{ 0 };
    std::atomic<std::uint64_t> leader_too_large_for_handoff_total{ 0 };
    std::atomic<std::uint64_t> follower_too_large_for_handoff_total{ 0 };
    std::atomic<std::uint64_t> follower_timeout_l2_body_too_large_total{ 0 };
};

void record_coalescing_event(CoalescingMetrics* metrics, CoalescingMetricEvent event);

// Renders all counters as Prometheus text into buf.
// Returns bytes written (excluding null terminator), or 0 on overflow / null buf.
std::size_t render_coalescing_metrics_prometheus(const CoalescingMetrics& metrics, char* buf,
                                                 std::size_t buf_size);

} // namespace bytetaper::metrics

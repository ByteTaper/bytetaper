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
    EntryCleanup,
    AttachSuccess,
    AttachFailureShardFull,
    AttachFailureMaxWaiters,
    AttachFailureStateMismatch,
    ResultReadyRetention,
    CleanupAfterRetention,
    TerminalResultServed,
    Wait,
    Wakeup,
    Timeout,
    LeaderUpstream,
    LeaderPublish,
    LeaderNotify,
    GroupInvariantFailures
};

enum class UpstreamCallReason : std::uint8_t {
    LeaderFill = 0,
    FollowerTimeoutFallback = 1,
    Bypass = 2,
    CoalescingDisabled = 3,
    ErrorRecovery = 4,
    NoInflightEntry = 5,
    EntryAlreadyTerminal = 6,
    Unknown = 7,
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
    std::atomic<std::uint64_t> upstream_call_reason_no_inflight_entry_total{ 0 };
    std::atomic<std::uint64_t> upstream_call_reason_entry_already_terminal_total{ 0 };
    std::atomic<std::uint64_t> upstream_call_reason_unknown_total{ 0 };

    // Expanded BT-037F metrics
    std::atomic<std::uint64_t> coalescing_attach_success_total{ 0 };
    std::atomic<std::uint64_t> coalescing_attach_failure_shard_full_total{ 0 };
    std::atomic<std::uint64_t> coalescing_attach_failure_max_waiters_total{ 0 };
    std::atomic<std::uint64_t> coalescing_attach_failure_state_mismatch_total{ 0 };
    std::atomic<std::uint64_t> coalescing_result_ready_retention_total{ 0 };
    std::atomic<std::uint64_t> coalescing_cleanup_after_retention_total{ 0 };
    std::atomic<std::uint64_t> coalescing_terminal_result_served_total{ 0 };
    std::atomic<std::uint64_t> coalescing_wait_total{ 0 };
    std::atomic<std::uint64_t> coalescing_wakeup_total{ 0 };
    std::atomic<std::uint64_t> coalescing_timeout_total{ 0 };
    std::atomic<std::uint64_t> coalescing_leader_upstream_total{ 0 };
    std::atomic<std::uint64_t> coalescing_leader_publish_total{ 0 };
    std::atomic<std::uint64_t> coalescing_leader_notify_total{ 0 };
    std::atomic<std::uint64_t> coalescing_group_invariant_failures_total{ 0 };
};

void record_coalescing_event(CoalescingMetrics* metrics, CoalescingMetricEvent event);
void record_upstream_call_reason(CoalescingMetrics* metrics, UpstreamCallReason reason);

// Renders all counters as Prometheus text into buf.
// Returns bytes written (excluding null terminator), or 0 on overflow / null buf.
std::size_t render_coalescing_metrics_prometheus(const CoalescingMetrics& metrics, char* buf,
                                                 std::size_t buf_size);

} // namespace bytetaper::metrics

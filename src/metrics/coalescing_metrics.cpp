// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/coalescing_metrics.h"

#include <cstdio>

namespace bytetaper::metrics {

void record_coalescing_event(CoalescingMetrics* metrics, CoalescingMetricEvent event) {
    if (metrics == nullptr) {
        return;
    }

    switch (event) {
    case CoalescingMetricEvent::Leader:
        metrics->leader_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::Follower:
        metrics->follower_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerCacheHit:
        metrics->follower_cache_hit_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::Fallback:
        metrics->fallback_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::Bypass:
        metrics->bypass_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::TooManyWaiters:
        metrics->too_many_waiters_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderResultPublished:
        metrics->leader_result_published_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderResultPublishFailed:
        metrics->leader_result_publish_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerServedFromResult:
        metrics->follower_served_from_result_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerTimeout:
        metrics->follower_timeout_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerFallback:
        metrics->follower_fallback_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::EntryCleanup:
        metrics->entry_cleanup_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::AttachSuccess:
        metrics->coalescing_attach_success_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::AttachFailureShardFull:
        metrics->coalescing_attach_failure_shard_full_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::AttachFailureMaxWaiters:
        metrics->coalescing_attach_failure_max_waiters_total.fetch_add(1,
                                                                       std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::AttachFailureStateMismatch:
        metrics->coalescing_attach_failure_state_mismatch_total.fetch_add(
            1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::ResultReadyRetention:
        metrics->coalescing_result_ready_retention_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::CleanupAfterRetention:
        metrics->coalescing_cleanup_after_retention_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::TerminalResultServed:
        metrics->coalescing_terminal_result_served_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::Wait:
        metrics->coalescing_wait_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::Wakeup:
        metrics->coalescing_wakeup_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::Timeout:
        metrics->coalescing_timeout_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderUpstream:
        metrics->coalescing_leader_upstream_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderPublish:
        metrics->coalescing_leader_publish_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderNotify:
        metrics->coalescing_leader_notify_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::GroupInvariantFailures:
        metrics->coalescing_group_invariant_failures_total.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

void record_upstream_call_reason(CoalescingMetrics* metrics, UpstreamCallReason reason) {
    if (metrics == nullptr) {
        return;
    }
    switch (reason) {
    case UpstreamCallReason::LeaderFill:
        metrics->upstream_call_reason_leader_fill_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case UpstreamCallReason::FollowerTimeoutFallback:
        metrics->upstream_call_reason_follower_timeout_total.fetch_add(1,
                                                                       std::memory_order_relaxed);
        break;
    case UpstreamCallReason::Bypass:
        metrics->upstream_call_reason_bypass_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case UpstreamCallReason::CoalescingDisabled:
        metrics->upstream_call_reason_coalescing_disabled_total.fetch_add(
            1, std::memory_order_relaxed);
        break;
    case UpstreamCallReason::ErrorRecovery:
        metrics->upstream_call_reason_error_recovery_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case UpstreamCallReason::NoInflightEntry:
        metrics->upstream_call_reason_no_inflight_entry_total.fetch_add(1,
                                                                        std::memory_order_relaxed);
        break;
    case UpstreamCallReason::EntryAlreadyTerminal:
        metrics->upstream_call_reason_entry_already_terminal_total.fetch_add(
            1, std::memory_order_relaxed);
        break;
    case UpstreamCallReason::Unknown:
        metrics->upstream_call_reason_unknown_total.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

std::size_t render_coalescing_metrics_prometheus(const CoalescingMetrics& metrics, char* buf,
                                                 std::size_t buf_size) {
    if (buf == nullptr || buf_size == 0) {
        return 0;
    }

    int written = std::snprintf(
        buf, buf_size,
        "# HELP bytetaper_coalescing_leader_total Total number of requests that became coalescing "
        "leaders.\n"
        "# TYPE bytetaper_coalescing_leader_total counter\n"
        "bytetaper_coalescing_leader_total %llu\n"
        "# HELP bytetaper_coalescing_follower_total Total number of requests that became "
        "coalescing followers.\n"
        "# TYPE bytetaper_coalescing_follower_total counter\n"
        "bytetaper_coalescing_follower_total %llu\n"
        "# HELP bytetaper_coalescing_follower_cache_hit_total Total number of followers that "
        "served from cache.\n"
        "# TYPE bytetaper_coalescing_follower_cache_hit_total counter\n"
        "bytetaper_coalescing_follower_cache_hit_total %llu\n"
        "# HELP bytetaper_coalescing_fallback_total Total number of followers that fell back to "
        "independent upstream fetch.\n"
        "# TYPE bytetaper_coalescing_fallback_total counter\n"
        "bytetaper_coalescing_fallback_total %llu\n"
        "# HELP bytetaper_coalescing_bypass_total Total number of requests that bypassed "
        "coalescing.\n"
        "# TYPE bytetaper_coalescing_bypass_total counter\n"
        "bytetaper_coalescing_bypass_total %llu\n"
        "# HELP bytetaper_coalescing_too_many_waiters_total Total number of requests rejected due "
        "to queue full.\n"
        "# TYPE bytetaper_coalescing_too_many_waiters_total counter\n"
        "bytetaper_coalescing_too_many_waiters_total %llu\n"
        "# HELP bytetaper_coalescing_leader_result_published_total Total leaders that successfully "
        "published result.\n"
        "# TYPE bytetaper_coalescing_leader_result_published_total counter\n"
        "bytetaper_coalescing_leader_result_published_total %llu\n"
        "# HELP bytetaper_coalescing_leader_result_publish_failed_total Total leaders that failed "
        "to publish result.\n"
        "# TYPE bytetaper_coalescing_leader_result_publish_failed_total counter\n"
        "bytetaper_coalescing_leader_result_publish_failed_total %llu\n"
        "# HELP bytetaper_coalescing_follower_served_from_result_total Total followers served from "
        "coalesced result.\n"
        "# TYPE bytetaper_coalescing_follower_served_from_result_total counter\n"
        "bytetaper_coalescing_follower_served_from_result_total %llu\n"
        "# HELP bytetaper_coalescing_follower_timeout_total Total followers that timed out.\n"
        "# TYPE bytetaper_coalescing_follower_timeout_total counter\n"
        "bytetaper_coalescing_follower_timeout_total %llu\n"
        "# HELP bytetaper_coalescing_follower_fallback_total Total followers that fell back.\n"
        "# TYPE bytetaper_coalescing_follower_fallback_total counter\n"
        "bytetaper_coalescing_follower_fallback_total %llu\n"
        "# HELP bytetaper_coalescing_entry_cleanup_total Total coalescing entries cleaned up.\n"
        "# TYPE bytetaper_coalescing_entry_cleanup_total counter\n"
        "bytetaper_coalescing_entry_cleanup_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_leader_fill_total Upstream calls due to leader "
        "fill.\n"
        "# TYPE bytetaper_upstream_call_reason_leader_fill_total counter\n"
        "bytetaper_upstream_call_reason_leader_fill_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_follower_timeout_total Upstream calls due to "
        "follower timeout.\n"
        "# TYPE bytetaper_upstream_call_reason_follower_timeout_total counter\n"
        "bytetaper_upstream_call_reason_follower_timeout_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_bypass_total Upstream calls due to bypass.\n"
        "# TYPE bytetaper_upstream_call_reason_bypass_total counter\n"
        "bytetaper_upstream_call_reason_bypass_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_coalescing_disabled_total Upstream calls due to "
        "coalescing disabled.\n"
        "# TYPE bytetaper_upstream_call_reason_coalescing_disabled_total counter\n"
        "bytetaper_upstream_call_reason_coalescing_disabled_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_error_recovery_total Upstream calls due to error "
        "recovery.\n"
        "# TYPE bytetaper_upstream_call_reason_error_recovery_total counter\n"
        "bytetaper_upstream_call_reason_error_recovery_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_no_inflight_entry_total Upstream calls due to no "
        "inflight entry.\n"
        "# TYPE bytetaper_upstream_call_reason_no_inflight_entry_total counter\n"
        "bytetaper_upstream_call_reason_no_inflight_entry_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_entry_already_terminal_total Upstream calls due to "
        "entry already terminal.\n"
        "# TYPE bytetaper_upstream_call_reason_entry_already_terminal_total counter\n"
        "bytetaper_upstream_call_reason_entry_already_terminal_total %llu\n"
        "# HELP bytetaper_upstream_call_reason_unknown_total Upstream calls due to unknown.\n"
        "# TYPE bytetaper_upstream_call_reason_unknown_total counter\n"
        "bytetaper_upstream_call_reason_unknown_total %llu\n"
        "# HELP bytetaper_coalescing_attach_success_total Total successful attachments.\n"
        "# TYPE bytetaper_coalescing_attach_success_total counter\n"
        "bytetaper_coalescing_attach_success_total %llu\n"
        "# HELP bytetaper_coalescing_attach_failure_shard_full_total Attach failures because shard "
        "is full.\n"
        "# TYPE bytetaper_coalescing_attach_failure_shard_full_total counter\n"
        "bytetaper_coalescing_attach_failure_shard_full_total %llu\n"
        "# HELP bytetaper_coalescing_attach_failure_max_waiters_total Attach failures because max "
        "waiters limit is exceeded.\n"
        "# TYPE bytetaper_coalescing_attach_failure_max_waiters_total counter\n"
        "bytetaper_coalescing_attach_failure_max_waiters_total %llu\n"
        "# HELP bytetaper_coalescing_attach_failure_state_mismatch_total Attach failures due to "
        "state mismatch.\n"
        "# TYPE bytetaper_coalescing_attach_failure_state_mismatch_total counter\n"
        "bytetaper_coalescing_attach_failure_state_mismatch_total %llu\n"
        "# HELP bytetaper_coalescing_result_ready_retention_total Requests that hit result-ready "
        "retention.\n"
        "# TYPE bytetaper_coalescing_result_ready_retention_total counter\n"
        "bytetaper_coalescing_result_ready_retention_total %llu\n"
        "# HELP bytetaper_coalescing_cleanup_after_retention_total Cleanups after retention "
        "expires.\n"
        "# TYPE bytetaper_coalescing_cleanup_after_retention_total counter\n"
        "bytetaper_coalescing_cleanup_after_retention_total %llu\n"
        "# HELP bytetaper_coalescing_terminal_result_served_total Followers served directly from "
        "result ready.\n"
        "# TYPE bytetaper_coalescing_terminal_result_served_total counter\n"
        "bytetaper_coalescing_terminal_result_served_total %llu\n"
        "# HELP bytetaper_coalescing_wait_total Followers entering wait.\n"
        "# TYPE bytetaper_coalescing_wait_total counter\n"
        "bytetaper_coalescing_wait_total %llu\n"
        "# HELP bytetaper_coalescing_wakeup_total Followers woken up.\n"
        "# TYPE bytetaper_coalescing_wakeup_total counter\n"
        "bytetaper_coalescing_wakeup_total %llu\n"
        "# HELP bytetaper_coalescing_timeout_total Followers that timed out waiting.\n"
        "# TYPE bytetaper_coalescing_timeout_total counter\n"
        "bytetaper_coalescing_timeout_total %llu\n"
        "# HELP bytetaper_coalescing_leader_upstream_total Leaders proceeding upstream.\n"
        "# TYPE bytetaper_coalescing_leader_upstream_total counter\n"
        "bytetaper_coalescing_leader_upstream_total %llu\n"
        "# HELP bytetaper_coalescing_leader_publish_total Leaders publishing results.\n"
        "# TYPE bytetaper_coalescing_leader_publish_total counter\n"
        "bytetaper_coalescing_leader_publish_total %llu\n"
        "# HELP bytetaper_coalescing_leader_notify_total Leaders notifying waiters.\n"
        "# TYPE bytetaper_coalescing_leader_notify_total counter\n"
        "bytetaper_coalescing_leader_notify_total %llu\n"
        "# HELP bytetaper_coalescing_group_invariant_failures_total Group invariant failures.\n"
        "# TYPE bytetaper_coalescing_group_invariant_failures_total counter\n"
        "bytetaper_coalescing_group_invariant_failures_total %llu\n",
        (unsigned long long) metrics.leader_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_cache_hit_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.fallback_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.bypass_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.too_many_waiters_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.leader_result_published_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.leader_result_publish_failed_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_served_from_result_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_timeout_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_fallback_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.entry_cleanup_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_leader_fill_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_follower_timeout_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_bypass_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_coalescing_disabled_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_error_recovery_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_no_inflight_entry_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_entry_already_terminal_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.upstream_call_reason_unknown_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_attach_success_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_attach_failure_shard_full_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_attach_failure_max_waiters_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_attach_failure_state_mismatch_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_result_ready_retention_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_cleanup_after_retention_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_terminal_result_served_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_wait_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_wakeup_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_timeout_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_leader_upstream_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_leader_publish_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_leader_notify_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.coalescing_group_invariant_failures_total.load(
            std::memory_order_relaxed));

    if (written < 0 || static_cast<std::size_t>(written) >= buf_size) {
        return 0;
    }

    return static_cast<std::size_t>(written);
}

} // namespace bytetaper::metrics

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
        "# HELP bytetaper_upstream_call_reason_unknown_total Upstream calls due to unknown.\n"
        "# TYPE bytetaper_upstream_call_reason_unknown_total counter\n"
        "bytetaper_upstream_call_reason_unknown_total %llu\n",
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
        (unsigned long long) metrics.upstream_call_reason_unknown_total.load(
            std::memory_order_relaxed));

    if (written < 0 || static_cast<std::size_t>(written) >= buf_size) {
        return 0;
    }

    return static_cast<std::size_t>(written);
}

} // namespace bytetaper::metrics

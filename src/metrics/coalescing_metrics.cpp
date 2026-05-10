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
    case CoalescingMetricEvent::FollowerTimeoutBeforePublish:
        metrics->follower_timeout_before_publish_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerTimeoutAfterPublish:
        metrics->follower_timeout_after_publish_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FallbackDeadlineUnknown:
        metrics->fallback_deadline_unknown_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerSharedResponse:
        metrics->follower_shared_response_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerL1Hit:
        metrics->follower_l1_hit_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerTimeout:
        metrics->follower_timeout_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerMissing:
        metrics->follower_missing_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerStoredButNoSnapshot:
        metrics->follower_stored_but_no_snapshot_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerNotCacheable:
        metrics->follower_not_cacheable_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerFailed:
        metrics->follower_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerUnaccounted:
        metrics->follower_unaccounted_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderL1StoreSuccess:
        metrics->leader_l1_store_success_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderL1StoreFailed:
        metrics->leader_l1_store_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerL1Ready:
        metrics->follower_l1_ready_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerL1ReadyButMiss:
        metrics->follower_l1_ready_but_miss_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerExpired:
        metrics->follower_expired_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderL2HandoffPending:
        metrics->leader_l2_handoff_pending_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderL2HandoffReady:
        metrics->leader_l2_handoff_ready_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderL2HandoffFailed:
        metrics->leader_l2_handoff_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerL2Ready:
        metrics->follower_l2_ready_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerL2Hit:
        metrics->follower_l2_hit_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerL2ReadyButMiss:
        metrics->follower_l2_ready_but_miss_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::LeaderTooLargeForHandoff:
        metrics->leader_too_large_for_handoff_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerTooLargeForHandoff:
        metrics->follower_too_large_for_handoff_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CoalescingMetricEvent::FollowerTimeoutL2BodyTooLarge:
        metrics->follower_timeout_l2_body_too_large_total.fetch_add(1, std::memory_order_relaxed);
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
        "# HELP bytetaper_coalescing_follower_timeout_before_publish_total Total number of "
        "follower timeout fallbacks before leader publish.\n"
        "# TYPE bytetaper_coalescing_follower_timeout_before_publish_total counter\n"
        "bytetaper_coalescing_follower_timeout_before_publish_total %llu\n"
        "# HELP bytetaper_coalescing_follower_timeout_after_publish_total Total number of follower "
        "timeout fallbacks after leader publish.\n"
        "# TYPE bytetaper_coalescing_follower_timeout_after_publish_total counter\n"
        "bytetaper_coalescing_follower_timeout_after_publish_total %llu\n"
        "# HELP bytetaper_coalescing_fallback_deadline_unknown_total Total number of fallbacks "
        "with unknown deadlines.\n"
        "# TYPE bytetaper_coalescing_fallback_deadline_unknown_total counter\n"
        "bytetaper_coalescing_fallback_deadline_unknown_total %llu\n"
        "# HELP bytetaper_coalescing_follower_shared_response_total Total number of followers that "
        "successfully got a shared response.\n"
        "# TYPE bytetaper_coalescing_follower_shared_response_total counter\n"
        "bytetaper_coalescing_follower_shared_response_total %llu\n"
        "# HELP bytetaper_coalescing_follower_l1_hit_total Total number of followers that hit L1 "
        "cache.\n"
        "# TYPE bytetaper_coalescing_follower_l1_hit_total counter\n"
        "bytetaper_coalescing_follower_l1_hit_total %llu\n"
        "# HELP bytetaper_coalescing_follower_timeout_total Total number of followers that timed "
        "out waiting on CV.\n"
        "# TYPE bytetaper_coalescing_follower_timeout_total counter\n"
        "bytetaper_coalescing_follower_timeout_total %llu\n"
        "# HELP bytetaper_coalescing_follower_missing_total Total number of followers that found "
        "registry entry missing / stale.\n"
        "# TYPE bytetaper_coalescing_follower_missing_total counter\n"
        "bytetaper_coalescing_follower_missing_total %llu\n"
        "# HELP bytetaper_coalescing_follower_stored_but_no_snapshot_total Total number of "
        "followers that found stored state but no snapshot ready.\n"
        "# TYPE bytetaper_coalescing_follower_stored_but_no_snapshot_total counter\n"
        "bytetaper_coalescing_follower_stored_but_no_snapshot_total %llu\n"
        "# HELP bytetaper_coalescing_follower_not_cacheable_total Total number of followers that "
        "found leader response was not cacheable.\n"
        "# TYPE bytetaper_coalescing_follower_not_cacheable_total counter\n"
        "bytetaper_coalescing_follower_not_cacheable_total %llu\n"
        "# HELP bytetaper_coalescing_follower_failed_total Total number of followers that found "
        "leader failed.\n"
        "# TYPE bytetaper_coalescing_follower_failed_total counter\n"
        "bytetaper_coalescing_follower_failed_total %llu\n"
        "# HELP bytetaper_coalescing_follower_unaccounted_total Total number of followers that "
        "went unaccounted.\n"
        "# TYPE bytetaper_coalescing_follower_unaccounted_total counter\n"
        "bytetaper_coalescing_follower_unaccounted_total %llu\n"
        "# HELP bytetaper_coalescing_leader_l1_store_success_total Total number of leader L1 store "
        "successes.\n"
        "# TYPE bytetaper_coalescing_leader_l1_store_success_total counter\n"
        "bytetaper_coalescing_leader_l1_store_success_total %llu\n"
        "# HELP bytetaper_coalescing_leader_l1_store_failed_total Total number of leader L1 store "
        "failures.\n"
        "# TYPE bytetaper_coalescing_leader_l1_store_failed_total counter\n"
        "bytetaper_coalescing_leader_l1_store_failed_total %llu\n"
        "# HELP bytetaper_coalescing_follower_l1_ready_total Total number of follower L1Ready "
        "wakeups.\n"
        "# TYPE bytetaper_coalescing_follower_l1_ready_total counter\n"
        "bytetaper_coalescing_follower_l1_ready_total %llu\n"
        "# HELP bytetaper_coalescing_follower_l1_ready_but_miss_total Total number of follower "
        "L1Ready wakeups that missed L1 lookup.\n"
        "# TYPE bytetaper_coalescing_follower_l1_ready_but_miss_total counter\n"
        "bytetaper_coalescing_follower_l1_ready_but_miss_total %llu\n"
        "# HELP bytetaper_coalescing_follower_expired_total Total number of registered followers "
        "expired waiting on CV.\n"
        "# TYPE bytetaper_coalescing_follower_expired_total counter\n"
        "bytetaper_coalescing_follower_expired_total %llu\n"
        "# HELP bytetaper_coalescing_leader_l2_handoff_pending_total Total number of leader L2 "
        "handoffs pending.\n"
        "# TYPE bytetaper_coalescing_leader_l2_handoff_pending_total counter\n"
        "bytetaper_coalescing_leader_l2_handoff_pending_total %llu\n"
        "# HELP bytetaper_coalescing_leader_l2_handoff_ready_total Total number of successful "
        "leader L2 handoffs.\n"
        "# TYPE bytetaper_coalescing_leader_l2_handoff_ready_total counter\n"
        "bytetaper_coalescing_leader_l2_handoff_ready_total %llu\n"
        "# HELP bytetaper_coalescing_leader_l2_handoff_failed_total Total number of failed leader "
        "L2 handoffs.\n"
        "# TYPE bytetaper_coalescing_leader_l2_handoff_failed_total counter\n"
        "bytetaper_coalescing_leader_l2_handoff_failed_total %llu\n"
        "# HELP bytetaper_coalescing_follower_l2_ready_total Total number of follower L2Ready "
        "wakeups.\n"
        "# TYPE bytetaper_coalescing_follower_l2_ready_total counter\n"
        "bytetaper_coalescing_follower_l2_ready_total %llu\n"
        "# HELP bytetaper_coalescing_follower_l2_hit_total Total number of follower L2 cache "
        "hits.\n"
        "# TYPE bytetaper_coalescing_follower_l2_hit_total counter\n"
        "bytetaper_coalescing_follower_l2_hit_total %llu\n"
        "# HELP bytetaper_coalescing_follower_l2_ready_but_miss_total Total number of follower "
        "L2Ready wakeups that missed L2 lookup.\n"
        "# TYPE bytetaper_coalescing_follower_l2_ready_but_miss_total counter\n"
        "bytetaper_coalescing_follower_l2_ready_but_miss_total %llu\n"
        "# HELP bytetaper_coalescing_leader_too_large_for_handoff_total Total number of leader "
        "requests "
        "where the response body exceeded follower buffer limits but was saved to L2.\n"
        "# TYPE bytetaper_coalescing_leader_too_large_for_handoff_total counter\n"
        "bytetaper_coalescing_leader_too_large_for_handoff_total %llu\n"
        "# HELP bytetaper_coalescing_follower_too_large_for_handoff_total Total number of follower "
        "requests "
        "that woke up to find the leader stored the body but it was too large for handoff.\n"
        "# TYPE bytetaper_coalescing_follower_too_large_for_handoff_total counter\n"
        "bytetaper_coalescing_follower_too_large_for_handoff_total %llu\n"
        "# HELP bytetaper_coalescing_follower_timeout_l2_body_too_large_total Total number of "
        "follower requests "
        "that timed out, checked L2, but found the body too large for the local buffer.\n"
        "# TYPE bytetaper_coalescing_follower_timeout_l2_body_too_large_total counter\n"
        "bytetaper_coalescing_follower_timeout_l2_body_too_large_total %llu\n",
        (unsigned long long) metrics.leader_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_cache_hit_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.fallback_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.bypass_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.too_many_waiters_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_timeout_before_publish_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_timeout_after_publish_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.fallback_deadline_unknown_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_shared_response_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_l1_hit_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_timeout_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_missing_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_stored_but_no_snapshot_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_not_cacheable_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_failed_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_unaccounted_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.leader_l1_store_success_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.leader_l1_store_failed_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_l1_ready_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_l1_ready_but_miss_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_expired_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.leader_l2_handoff_pending_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.leader_l2_handoff_ready_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.leader_l2_handoff_failed_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_l2_ready_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_l2_hit_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.follower_l2_ready_but_miss_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.leader_too_large_for_handoff_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_too_large_for_handoff_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.follower_timeout_l2_body_too_large_total.load(
            std::memory_order_relaxed));

    if (written < 0 || static_cast<std::size_t>(written) >= buf_size) {
        return 0;
    }

    return static_cast<std::size_t>(written);
}

} // namespace bytetaper::metrics

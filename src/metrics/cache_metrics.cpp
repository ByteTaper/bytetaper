// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/cache_metrics.h"

#include <cstdio>

namespace bytetaper::metrics {

void record_cache_event(CacheMetrics* metrics, CacheMetricEvent event) {
    if (metrics == nullptr)
        return;
    switch (event) {
    case CacheMetricEvent::L1Hit:
        metrics->l1_hit.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1Miss:
        metrics->l1_miss.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L2Hit:
        metrics->l2_hit.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L2Miss:
        metrics->l2_miss.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::Store:
        metrics->store.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::Expired:
        metrics->expired.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::Bypass:
        metrics->bypass.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1StoreAttempt:
        metrics->l1_store_attempt.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1StoreSuccess:
        metrics->l1_store_success.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1StoreSkipped:
        metrics->l1_store_skipped.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1StoreSkippedBodyTooLarge:
        metrics->l1_store_skipped_body_too_large.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

std::size_t render_cache_metrics_prometheus(const CacheMetrics& metrics, char* buf,
                                            std::size_t buf_size) {
    if (buf == nullptr || buf_size == 0)
        return 0;
    int n =
        std::snprintf(buf, buf_size,
                      "# HELP bytetaper_cache_l1_hit_total Cache L1 hit count\n"
                      "# TYPE bytetaper_cache_l1_hit_total counter\n"
                      "bytetaper_cache_l1_hit_total %llu\n"
                      "# HELP bytetaper_cache_l1_miss_total Cache L1 miss count\n"
                      "# TYPE bytetaper_cache_l1_miss_total counter\n"
                      "bytetaper_cache_l1_miss_total %llu\n"
                      "# HELP bytetaper_cache_l2_hit_total Cache L2 hit count\n"
                      "# TYPE bytetaper_cache_l2_hit_total counter\n"
                      "bytetaper_cache_l2_hit_total %llu\n"
                      "# HELP bytetaper_cache_l2_miss_total Cache L2 miss count\n"
                      "# TYPE bytetaper_cache_l2_miss_total counter\n"
                      "bytetaper_cache_l2_miss_total %llu\n"
                      "# HELP bytetaper_cache_store_total Cache store count\n"
                      "# TYPE bytetaper_cache_store_total counter\n"
                      "bytetaper_cache_store_total %llu\n"
                      "# HELP bytetaper_cache_expired_total Cache expired entry count\n"
                      "# TYPE bytetaper_cache_expired_total counter\n"
                      "bytetaper_cache_expired_total %llu\n"
                      "# HELP bytetaper_cache_bypass_total Cache auth bypass count\n"
                      "# TYPE bytetaper_cache_bypass_total counter\n"
                      "bytetaper_cache_bypass_total %llu\n"
                      "# HELP bytetaper_cache_l1_store_attempt_total Cache L1 store attempt count\n"
                      "# TYPE bytetaper_cache_l1_store_attempt_total counter\n"
                      "bytetaper_cache_l1_store_attempt_total %llu\n"
                      "# HELP bytetaper_cache_l1_store_success_total Cache L1 store success count\n"
                      "# TYPE bytetaper_cache_l1_store_success_total counter\n"
                      "bytetaper_cache_l1_store_success_total %llu\n"
                      "# HELP bytetaper_cache_l1_store_skipped_total Cache L1 store skipped count\n"
                      "# TYPE bytetaper_cache_l1_store_skipped_total counter\n"
                      "bytetaper_cache_l1_store_skipped_total %llu\n"
                      "# HELP bytetaper_cache_l1_store_skipped_body_too_large_total Cache L1 store "
                      "skipped body too large count\n"
                      "# TYPE bytetaper_cache_l1_store_skipped_body_too_large_total counter\n"
                      "bytetaper_cache_l1_store_skipped_body_too_large_total %llu\n",
                      (unsigned long long) metrics.l1_hit.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.l1_miss.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.l2_hit.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.l2_miss.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.store.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.expired.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.bypass.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.l1_store_attempt.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.l1_store_success.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.l1_store_skipped.load(std::memory_order_relaxed),
                      (unsigned long long) metrics.l1_store_skipped_body_too_large.load(
                          std::memory_order_relaxed));
    if (n < 0 || static_cast<std::size_t>(n) >= buf_size)
        return 0;
    return static_cast<std::size_t>(n);
}

} // namespace bytetaper::metrics

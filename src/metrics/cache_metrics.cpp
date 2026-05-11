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
    case CacheMetricEvent::L1EvictionRingOverwrite:
        metrics->l1_eviction_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1DuplicateOverwrite:
        metrics->l1_duplicate_overwrite_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1ExpiredMiss:
        metrics->l1_expired_miss_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1LookupSlotScanned:
        metrics->l1_lookup_slots_scanned_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1StoreAdmitted:
        metrics->l1_store_admitted_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1StoreRejectedBodyTooLarge:
        metrics->l1_store_rejected_body_too_large_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1StoreRejectedInvalidBody:
        metrics->l1_store_rejected_invalid_body_total.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

std::size_t render_cache_metrics_prometheus(const CacheMetrics& metrics, char* buf,
                                            std::size_t buf_size) {
    if (buf == nullptr || buf_size == 0)
        return 0;
    int n = std::snprintf(
        buf, buf_size,
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
        "bytetaper_cache_l1_store_skipped_body_too_large_total %llu\n"
        "# HELP bytetaper_l1_eviction_total L1 entries overwritten by ring-buffer advancement\n"
        "# TYPE bytetaper_l1_eviction_total counter\n"
        "bytetaper_l1_eviction_total %llu\n"
        "# HELP bytetaper_l1_duplicate_overwrite_total L1 entries overwritten due to duplicate key "
        "promotion\n"
        "# TYPE bytetaper_l1_duplicate_overwrite_total counter\n"
        "bytetaper_l1_duplicate_overwrite_total %llu\n"
        "# HELP bytetaper_l1_expired_miss_total L1 lookup slots skipped due to TTL expiry\n"
        "# TYPE bytetaper_l1_expired_miss_total counter\n"
        "bytetaper_l1_expired_miss_total %llu\n"
        "# HELP bytetaper_l1_lookup_slots_scanned_total Total L1 shard slots inspected during key "
        "lookups\n"
        "# TYPE bytetaper_l1_lookup_slots_scanned_total counter\n"
        "bytetaper_l1_lookup_slots_scanned_total %llu\n"
        "# HELP bytetaper_l1_store_admitted_total L1 store admitted count\n"
        "# TYPE bytetaper_l1_store_admitted_total counter\n"
        "bytetaper_l1_store_admitted_total %llu\n"
        "# HELP bytetaper_l1_store_rejected_body_too_large_total L1 store rejected body too large "
        "count\n"
        "# TYPE bytetaper_l1_store_rejected_body_too_large_total counter\n"
        "bytetaper_l1_store_rejected_body_too_large_total %llu\n"
        "# HELP bytetaper_l1_store_rejected_invalid_body_total L1 store rejected invalid body "
        "count\n"
        "# TYPE bytetaper_l1_store_rejected_invalid_body_total counter\n"
        "bytetaper_l1_store_rejected_invalid_body_total %llu\n",
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
            std::memory_order_relaxed),
        (unsigned long long) metrics.l1_eviction_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_duplicate_overwrite_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_expired_miss_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_lookup_slots_scanned_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_store_admitted_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_store_rejected_body_too_large_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.l1_store_rejected_invalid_body_total.load(
            std::memory_order_relaxed));
    if (n < 0 || static_cast<std::size_t>(n) >= buf_size)
        return 0;
    return static_cast<std::size_t>(n);
}

} // namespace bytetaper::metrics

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
    case CacheMetricEvent::InvalidationPrepareAttempt:
        metrics->invalidation_prepare_attempt_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationPrepared:
        metrics->invalidation_prepared_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationPrepareSkipped:
        metrics->invalidation_prepare_skipped_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationApplyAttempt:
        metrics->invalidation_apply_attempt_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationApplied:
        metrics->invalidation_applied_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationSkipped:
        metrics->invalidation_skipped_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationFailed:
        metrics->invalidation_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationStatusNotSuccessful:
        metrics->invalidation_status_not_successful_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationNoPolicy:
        metrics->invalidation_no_policy_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationMethodNotEnabled:
        metrics->invalidation_method_not_enabled_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::InvalidationTargetResolveFailed:
        metrics->invalidation_target_resolve_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::RouteEpochBumpAttempt:
        metrics->route_epoch_bump_attempt_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::RouteEpochBumpSuccess:
        metrics->route_epoch_bump_success_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::RouteEpochBumpFailed:
        metrics->route_epoch_bump_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1RemoveHit:
        metrics->l1_remove_hit_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1RemoveMiss:
        metrics->l1_remove_miss_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L1RemoveFailed:
        metrics->l1_remove_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L2RemoveEnqueued:
        metrics->l2_remove_enqueued_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L2RemoveSuccess:
        metrics->l2_remove_success_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L2RemoveMiss:
        metrics->l2_remove_miss_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::L2RemoveFailed:
        metrics->l2_remove_failed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::VariantInvalidatedByRouteEpoch:
        metrics->variant_invalidated_by_route_epoch_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::VariantExactRemoveAttempt:
        metrics->variant_exact_remove_attempt_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::VariantExactRemoveSuccess:
        metrics->variant_exact_remove_success_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case CacheMetricEvent::VariantExactRemoveMiss:
        metrics->variant_exact_remove_miss_total.fetch_add(1, std::memory_order_relaxed);
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
        "bytetaper_l1_store_rejected_invalid_body_total %llu\n"
        "# HELP bytetaper_cache_invalidation_prepare_attempt_total Cache invalidation prepare "
        "attempt count\n"
        "# TYPE bytetaper_cache_invalidation_prepare_attempt_total counter\n"
        "bytetaper_cache_invalidation_prepare_attempt_total %llu\n"
        "# HELP bytetaper_cache_invalidation_prepared_total Cache invalidation prepared count\n"
        "# TYPE bytetaper_cache_invalidation_prepared_total counter\n"
        "bytetaper_cache_invalidation_prepared_total %llu\n"
        "# HELP bytetaper_cache_invalidation_prepare_skipped_total Cache invalidation prepare "
        "skipped count\n"
        "# TYPE bytetaper_cache_invalidation_prepare_skipped_total counter\n"
        "bytetaper_cache_invalidation_prepare_skipped_total %llu\n"
        "# HELP bytetaper_cache_invalidation_apply_attempt_total Cache invalidation apply attempt "
        "count\n"
        "# TYPE bytetaper_cache_invalidation_apply_attempt_total counter\n"
        "bytetaper_cache_invalidation_apply_attempt_total %llu\n"
        "# HELP bytetaper_cache_invalidation_applied_total Cache invalidation applied count\n"
        "# TYPE bytetaper_cache_invalidation_applied_total counter\n"
        "bytetaper_cache_invalidation_applied_total %llu\n"
        "# HELP bytetaper_cache_invalidation_skipped_total Cache invalidation skipped count\n"
        "# TYPE bytetaper_cache_invalidation_skipped_total counter\n"
        "bytetaper_cache_invalidation_skipped_total %llu\n"
        "# HELP bytetaper_cache_invalidation_failed_total Cache invalidation failed count\n"
        "# TYPE bytetaper_cache_invalidation_failed_total counter\n"
        "bytetaper_cache_invalidation_failed_total %llu\n"
        "# HELP bytetaper_cache_invalidation_status_not_successful_total Cache invalidation status "
        "not successful count\n"
        "# TYPE bytetaper_cache_invalidation_status_not_successful_total counter\n"
        "bytetaper_cache_invalidation_status_not_successful_total %llu\n"
        "# HELP bytetaper_cache_invalidation_no_policy_total Cache invalidation no policy count\n"
        "# TYPE bytetaper_cache_invalidation_no_policy_total counter\n"
        "bytetaper_cache_invalidation_no_policy_total %llu\n"
        "# HELP bytetaper_cache_invalidation_method_not_enabled_total Cache invalidation method "
        "not enabled count\n"
        "# TYPE bytetaper_cache_invalidation_method_not_enabled_total counter\n"
        "bytetaper_cache_invalidation_method_not_enabled_total %llu\n"
        "# HELP bytetaper_cache_invalidation_target_resolve_failed_total Cache invalidation target "
        "resolve failed count\n"
        "# TYPE bytetaper_cache_invalidation_target_resolve_failed_total counter\n"
        "bytetaper_cache_invalidation_target_resolve_failed_total %llu\n"
        "# HELP bytetaper_cache_route_epoch_bump_attempt_total Cache route epoch bump attempt "
        "count\n"
        "# TYPE bytetaper_cache_route_epoch_bump_attempt_total counter\n"
        "bytetaper_cache_route_epoch_bump_attempt_total %llu\n"
        "# HELP bytetaper_cache_route_epoch_bump_success_total Cache route epoch bump success "
        "count\n"
        "# TYPE bytetaper_cache_route_epoch_bump_success_total counter\n"
        "bytetaper_cache_route_epoch_bump_success_total %llu\n"
        "# HELP bytetaper_cache_route_epoch_bump_failed_total Cache route epoch bump failed count\n"
        "# TYPE bytetaper_cache_route_epoch_bump_failed_total counter\n"
        "bytetaper_cache_route_epoch_bump_failed_total %llu\n"
        "# HELP bytetaper_cache_l1_remove_hit_total Cache L1 remove hit count\n"
        "# TYPE bytetaper_cache_l1_remove_hit_total counter\n"
        "bytetaper_cache_l1_remove_hit_total %llu\n"
        "# HELP bytetaper_cache_l1_remove_miss_total Cache L1 remove miss count\n"
        "# TYPE bytetaper_cache_l1_remove_miss_total counter\n"
        "bytetaper_cache_l1_remove_miss_total %llu\n"
        "# HELP bytetaper_cache_l1_remove_failed_total Cache L1 remove failed count\n"
        "# TYPE bytetaper_cache_l1_remove_failed_total counter\n"
        "bytetaper_cache_l1_remove_failed_total %llu\n"
        "# HELP bytetaper_cache_l2_remove_enqueued_total Cache L2 remove enqueued count\n"
        "# TYPE bytetaper_cache_l2_remove_enqueued_total counter\n"
        "bytetaper_cache_l2_remove_enqueued_total %llu\n"
        "# HELP bytetaper_cache_l2_remove_success_total Cache L2 remove success count\n"
        "# TYPE bytetaper_cache_l2_remove_success_total counter\n"
        "bytetaper_cache_l2_remove_success_total %llu\n"
        "# HELP bytetaper_cache_l2_remove_miss_total Cache L2 remove miss count\n"
        "# TYPE bytetaper_cache_l2_remove_miss_total counter\n"
        "bytetaper_cache_l2_remove_miss_total %llu\n"
        "# HELP bytetaper_cache_l2_remove_failed_total Cache L2 remove failed count\n"
        "# TYPE bytetaper_cache_l2_remove_failed_total counter\n"
        "bytetaper_cache_l2_remove_failed_total %llu\n"
        "# HELP bytetaper_cache_variant_invalidated_by_route_epoch_total Cache variant invalidated "
        "by route epoch count\n"
        "# TYPE bytetaper_cache_variant_invalidated_by_route_epoch_total counter\n"
        "bytetaper_cache_variant_invalidated_by_route_epoch_total %llu\n"
        "# HELP bytetaper_cache_variant_exact_remove_attempt_total Cache variant exact remove "
        "attempt count\n"
        "# TYPE bytetaper_cache_variant_exact_remove_attempt_total counter\n"
        "bytetaper_cache_variant_exact_remove_attempt_total %llu\n"
        "# HELP bytetaper_cache_variant_exact_remove_success_total Cache variant exact remove "
        "success count\n"
        "# TYPE bytetaper_cache_variant_exact_remove_success_total counter\n"
        "bytetaper_cache_variant_exact_remove_success_total %llu\n"
        "# HELP bytetaper_cache_variant_exact_remove_miss_total Cache variant exact remove miss "
        "count\n"
        "# TYPE bytetaper_cache_variant_exact_remove_miss_total counter\n"
        "bytetaper_cache_variant_exact_remove_miss_total %llu\n",
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
            std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_prepare_attempt_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_prepared_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_prepare_skipped_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_apply_attempt_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_applied_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_skipped_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_failed_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_status_not_successful_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_no_policy_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_method_not_enabled_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.invalidation_target_resolve_failed_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.route_epoch_bump_attempt_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.route_epoch_bump_success_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.route_epoch_bump_failed_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_remove_hit_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_remove_miss_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l1_remove_failed_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l2_remove_enqueued_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l2_remove_success_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l2_remove_miss_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.l2_remove_failed_total.load(std::memory_order_relaxed),
        (unsigned long long) metrics.variant_invalidated_by_route_epoch_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.variant_exact_remove_attempt_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.variant_exact_remove_success_total.load(
            std::memory_order_relaxed),
        (unsigned long long) metrics.variant_exact_remove_miss_total.load(
            std::memory_order_relaxed));
    if (n < 0 || static_cast<std::size_t>(n) >= buf_size)
        return 0;
    return static_cast<std::size_t>(n);
}

} // namespace bytetaper::metrics

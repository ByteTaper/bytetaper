// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bytetaper::metrics {

enum class CacheMetricEvent : std::uint8_t {
    L1Hit,
    L1Miss,
    L2Hit,
    L2Miss,
    Store,
    Expired,
    Bypass,
    L1StoreAttempt,
    L1StoreSuccess,
    L1StoreSkipped,
    L1StoreSkippedBodyTooLarge,
    L1EvictionRingOverwrite,
    L1DuplicateOverwrite,
    L1ExpiredMiss,
    L1LookupSlotScanned,
    L1StoreAdmitted,
    L1StoreRejectedBodyTooLarge,
    L1StoreRejectedInvalidBody,

    // Invalidation — prepare stage
    InvalidationPrepareAttempt,
    InvalidationPrepared,
    InvalidationPrepareSkipped,

    // Invalidation — apply stage
    InvalidationApplyAttempt,
    InvalidationApplied,
    InvalidationSkipped,
    InvalidationFailed,

    // Invalidation — skip/fail reasons
    InvalidationStatusNotSuccessful,
    InvalidationNoPolicy,
    InvalidationMethodNotEnabled,
    InvalidationTargetResolveFailed,

    // Route epoch
    RouteEpochBumpAttempt,
    RouteEpochBumpSuccess,
    RouteEpochBumpFailed,

    // L1 exact remove (BT-CINV-002)
    L1RemoveHit,
    L1RemoveMiss,
    L1RemoveFailed,

    // L2 async worker (BT-CINV-006)
    L2RemoveEnqueued,
    L2RemoveSuccess,
    L2RemoveMiss,
    L2RemoveFailed,

    // Variant cache (BT-CINV-007)
    VariantInvalidatedByRouteEpoch,
    VariantExactRemoveAttempt,
    VariantExactRemoveSuccess,
    VariantExactRemoveMiss,
};

struct CacheMetrics {
    std::atomic<std::uint64_t> l1_hit{ 0 };
    std::atomic<std::uint64_t> l1_miss{ 0 };
    std::atomic<std::uint64_t> l2_hit{ 0 };
    std::atomic<std::uint64_t> l2_miss{ 0 };
    std::atomic<std::uint64_t> store{ 0 };
    std::atomic<std::uint64_t> expired{ 0 };
    std::atomic<std::uint64_t> bypass{ 0 };
    std::atomic<std::uint64_t> l1_store_attempt{ 0 };
    std::atomic<std::uint64_t> l1_store_success{ 0 };
    std::atomic<std::uint64_t> l1_store_skipped{ 0 };
    std::atomic<std::uint64_t> l1_store_skipped_body_too_large{ 0 };
    std::atomic<std::uint64_t> l1_eviction_total{ 0 };
    std::atomic<std::uint64_t> l1_duplicate_overwrite_total{ 0 };
    std::atomic<std::uint64_t> l1_expired_miss_total{ 0 };
    std::atomic<std::uint64_t> l1_lookup_slots_scanned_total{ 0 };
    std::atomic<std::uint64_t> l1_store_admitted_total{ 0 };
    std::atomic<std::uint64_t> l1_store_rejected_body_too_large_total{ 0 };
    std::atomic<std::uint64_t> l1_store_rejected_invalid_body_total{ 0 };

    // Invalidation counters
    std::atomic<std::uint64_t> invalidation_prepare_attempt_total{ 0 };
    std::atomic<std::uint64_t> invalidation_prepared_total{ 0 };
    std::atomic<std::uint64_t> invalidation_prepare_skipped_total{ 0 };
    std::atomic<std::uint64_t> invalidation_apply_attempt_total{ 0 };
    std::atomic<std::uint64_t> invalidation_applied_total{ 0 };
    std::atomic<std::uint64_t> invalidation_skipped_total{ 0 };
    std::atomic<std::uint64_t> invalidation_failed_total{ 0 };
    std::atomic<std::uint64_t> invalidation_status_not_successful_total{ 0 };
    std::atomic<std::uint64_t> invalidation_no_policy_total{ 0 };
    std::atomic<std::uint64_t> invalidation_method_not_enabled_total{ 0 };
    std::atomic<std::uint64_t> invalidation_target_resolve_failed_total{ 0 };
    std::atomic<std::uint64_t> route_epoch_bump_attempt_total{ 0 };
    std::atomic<std::uint64_t> route_epoch_bump_success_total{ 0 };
    std::atomic<std::uint64_t> route_epoch_bump_failed_total{ 0 };
    std::atomic<std::uint64_t> l1_remove_hit_total{ 0 };
    std::atomic<std::uint64_t> l1_remove_miss_total{ 0 };
    std::atomic<std::uint64_t> l1_remove_failed_total{ 0 };
    std::atomic<std::uint64_t> l2_remove_enqueued_total{ 0 };
    std::atomic<std::uint64_t> l2_remove_success_total{ 0 };
    std::atomic<std::uint64_t> l2_remove_miss_total{ 0 };
    std::atomic<std::uint64_t> l2_remove_failed_total{ 0 };
    std::atomic<std::uint64_t> variant_invalidated_by_route_epoch_total{ 0 };
    std::atomic<std::uint64_t> variant_exact_remove_attempt_total{ 0 };
    std::atomic<std::uint64_t> variant_exact_remove_success_total{ 0 };
    std::atomic<std::uint64_t> variant_exact_remove_miss_total{ 0 };
};

void record_cache_event(CacheMetrics* metrics, CacheMetricEvent event);

// Renders Prometheus text into buf. Returns bytes written (excluding NUL), or 0 on overflow.
std::size_t render_cache_metrics_prometheus(const CacheMetrics& metrics, char* buf,
                                            std::size_t buf_size);

} // namespace bytetaper::metrics

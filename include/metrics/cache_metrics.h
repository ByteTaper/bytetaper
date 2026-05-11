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
};

void record_cache_event(CacheMetrics* metrics, CacheMetricEvent event);

// Renders Prometheus text into buf. Returns bytes written (excluding NUL), or 0 on overflow.
std::size_t render_cache_metrics_prometheus(const CacheMetrics& metrics, char* buf,
                                            std::size_t buf_size);

} // namespace bytetaper::metrics

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_METRICS_RUNTIME_METRICS_H
#define BYTETAPER_METRICS_RUNTIME_METRICS_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bytetaper::metrics {

enum class RuntimeMetricEvent : std::uint8_t {
    // Worker queue lifecycle
    Enqueue,
    EnqueueDropped,
    JobExecuted,
    JobError,
    // Async L2 lookup (enqueue stage)
    L2LookupEnqueued,
    L2LookupDeduped,
    // Async L2 lookup (worker)
    L2LookupHit,
    L2LookupMiss,
    L2LookupError,
    // Async L2 store (enqueue stage)
    L2StoreEnqueued,
    L2StoreDropped,
    L2StoreOversizedSkipped,
    // Async L2 store (worker)
    L2StoreSuccess,
    L2StoreError,
    // L2-to-L1 promotion (worker)
    L2ToL1Promotion,
    L2ToL1StaleRejected,
    L2ToL1PromotionSkippedBodyTooLarge,
};

struct RuntimeMetrics {
    // Worker queue counters
    std::atomic<std::uint64_t> worker_enqueue_total{ 0 };
    std::atomic<std::uint64_t> worker_enqueue_dropped_total{ 0 };
    std::atomic<std::uint64_t> worker_job_executed_total{ 0 };
    std::atomic<std::uint64_t> worker_job_error_total{ 0 };
    // Gauge fields
    std::atomic<std::uint64_t> worker_queue_depth{ 0 };
    std::atomic<std::uint64_t> worker_queue_capacity{ 0 };

    // Async L2 lookup
    std::atomic<std::uint64_t> l2_async_lookup_total{ 0 };
    std::atomic<std::uint64_t> l2_async_lookup_hit_total{ 0 };
    std::atomic<std::uint64_t> l2_async_lookup_miss_total{ 0 };
    std::atomic<std::uint64_t> l2_async_lookup_error_total{ 0 };
    std::atomic<std::uint64_t> l2_async_lookup_deduped_total{ 0 };

    // Async L2 store
    std::atomic<std::uint64_t> l2_async_store_total{ 0 };
    std::atomic<std::uint64_t> l2_async_store_success_total{ 0 };
    std::atomic<std::uint64_t> l2_async_store_error_total{ 0 };
    std::atomic<std::uint64_t> l2_async_store_dropped_total{ 0 };
    std::atomic<std::uint64_t> l2_async_store_oversized_skipped_total{ 0 };

    // L2-to-L1 promotion
    std::atomic<std::uint64_t> l2_to_l1_promotion_total{ 0 };
    std::atomic<std::uint64_t> l2_to_l1_stale_rejected_total{ 0 };
    std::atomic<std::uint64_t> l2_to_l1_promotion_skipped_body_too_large_total{ 0 };
};

void record_runtime_event(RuntimeMetrics* metrics, RuntimeMetricEvent event);

std::size_t render_runtime_metrics_prometheus(const RuntimeMetrics& metrics, char* buf,
                                              std::size_t buf_size);

} // namespace bytetaper::metrics

#endif // BYTETAPER_METRICS_RUNTIME_METRICS_H

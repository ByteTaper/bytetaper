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
    L2StoreEncodeError,
    L2StoreBodyTooLarge,
    // L2-to-L1 promotion (worker)
    L2ToL1Promotion,
    L2ToL1StaleRejected,
    L2ToL1PromotionSkippedBodyTooLarge,
    // Detailed L2 lookup results
    L2LookupBodyTooLargeForBuffer,
    L2LookupExpired,
    L2LookupDecodeError,
    L2LookupRocksDbError,
    // New metrics for BT-MAIN-005
    L2AsyncStoreBodyPoolFull,
    L2LookupLaneWait,
    L2StoreLaneWait,
    WorkerStoreLaneStarvation,
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
    std::atomic<std::uint64_t> l2_async_store_encode_error_total{ 0 };
    std::atomic<std::uint64_t> l2_async_store_body_too_large_total{ 0 };

    // L2-to-L1 promotion
    std::atomic<std::uint64_t> l2_to_l1_promotion_total{ 0 };
    std::atomic<std::uint64_t> l2_to_l1_stale_rejected_total{ 0 };
    std::atomic<std::uint64_t> l2_to_l1_promotion_skipped_body_too_large_total{ 0 };

    // Detailed L2 lookup counters
    std::atomic<std::uint64_t> l2_lookup_body_too_large_for_buffer_total{ 0 };
    std::atomic<std::uint64_t> l2_lookup_expired_total{ 0 };
    std::atomic<std::uint64_t> l2_lookup_decode_error_total{ 0 };
    std::atomic<std::uint64_t> l2_lookup_rocksdb_error_total{ 0 };

    // New metrics for BT-MAIN-005
    std::atomic<std::uint64_t> worker_store_body_pool_full_total{ 0 };
    std::atomic<std::uint64_t> worker_lookup_lane_wait_ms_total{ 0 };
    std::atomic<std::uint64_t> worker_lookup_lane_wait_count_total{ 0 };
    std::atomic<std::uint64_t> worker_store_lane_wait_ms_total{ 0 };
    std::atomic<std::uint64_t> worker_store_lane_wait_count_total{ 0 };
    std::atomic<std::uint64_t> worker_store_lane_starvation_total{ 0 };
    std::atomic<std::uint64_t> worker_store_body_pool_bytes_in_use{ 0 };
};

void record_runtime_event(RuntimeMetrics* metrics, RuntimeMetricEvent event);
void record_runtime_wait_ms(RuntimeMetrics* metrics, RuntimeMetricEvent event,
                            std::uint64_t wait_ms);

std::size_t render_runtime_metrics_prometheus(const RuntimeMetrics& metrics, char* buf,
                                              std::size_t buf_size);

} // namespace bytetaper::metrics

#endif // BYTETAPER_METRICS_RUNTIME_METRICS_H

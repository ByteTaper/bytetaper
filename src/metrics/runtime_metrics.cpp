// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/runtime_metrics.h"

#include <cstdio>

namespace bytetaper::metrics {

void record_runtime_event(RuntimeMetrics* metrics, RuntimeMetricEvent event) {
    if (metrics == nullptr) {
        return;
    }

    switch (event) {
    case RuntimeMetricEvent::Enqueue:
        metrics->worker_enqueue_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::EnqueueDropped:
        metrics->worker_enqueue_dropped_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::JobExecuted:
        metrics->worker_job_executed_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::JobError:
        metrics->worker_job_error_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupEnqueued:
        metrics->l2_async_lookup_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupDeduped:
        metrics->l2_async_lookup_deduped_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupHit:
        metrics->l2_async_lookup_hit_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupMiss:
        metrics->l2_async_lookup_miss_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupError:
        metrics->l2_async_lookup_error_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreEnqueued:
        metrics->l2_async_store_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreDropped:
        metrics->l2_async_store_dropped_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreOversizedSkipped:
        metrics->l2_async_store_oversized_skipped_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreSuccess:
        metrics->l2_async_store_success_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreError:
        metrics->l2_async_store_error_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreEncodeError:
        metrics->l2_async_store_encode_error_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreBodyTooLarge:
        metrics->l2_async_store_body_too_large_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2ToL1Promotion:
        metrics->l2_to_l1_promotion_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2ToL1StaleRejected:
        metrics->l2_to_l1_stale_rejected_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2ToL1PromotionSkippedBodyTooLarge:
        metrics->l2_to_l1_promotion_skipped_body_too_large_total.fetch_add(
            1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupBodyTooLargeForBuffer:
        metrics->l2_lookup_body_too_large_for_buffer_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupExpired:
        metrics->l2_lookup_expired_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupDecodeError:
        metrics->l2_lookup_decode_error_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2LookupRocksDbError:
        metrics->l2_lookup_rocksdb_error_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2AsyncStoreBodyPoolFull:
        metrics->worker_store_body_pool_full_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::WorkerStoreLaneStarvation:
        metrics->worker_store_lane_starvation_total.fetch_add(1, std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

void record_runtime_wait_ms(RuntimeMetrics* metrics, RuntimeMetricEvent event,
                            std::uint64_t wait_ms) {
    if (metrics == nullptr) {
        return;
    }
    switch (event) {
    case RuntimeMetricEvent::L2LookupLaneWait:
        metrics->worker_lookup_lane_wait_ms_total.fetch_add(wait_ms, std::memory_order_relaxed);
        metrics->worker_lookup_lane_wait_count_total.fetch_add(1, std::memory_order_relaxed);
        break;
    case RuntimeMetricEvent::L2StoreLaneWait:
        metrics->worker_store_lane_wait_ms_total.fetch_add(wait_ms, std::memory_order_relaxed);
        metrics->worker_store_lane_wait_count_total.fetch_add(1, std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

std::size_t render_runtime_metrics_prometheus(const RuntimeMetrics& metrics, char* buf,
                                              std::size_t buf_size) {
    if (buf == nullptr || buf_size == 0) {
        return 0;
    }

    const std::uint64_t lookup_wait_total =
        metrics.worker_lookup_lane_wait_ms_total.load(std::memory_order_relaxed);
    const std::uint64_t lookup_wait_count =
        metrics.worker_lookup_lane_wait_count_total.load(std::memory_order_relaxed);
    const double lookup_wait_avg =
        lookup_wait_count > 0 ? static_cast<double>(lookup_wait_total) / lookup_wait_count : 0.0;

    const std::uint64_t store_wait_total =
        metrics.worker_store_lane_wait_ms_total.load(std::memory_order_relaxed);
    const std::uint64_t store_wait_count =
        metrics.worker_store_lane_wait_count_total.load(std::memory_order_relaxed);
    const double store_wait_avg =
        store_wait_count > 0 ? static_cast<double>(store_wait_total) / store_wait_count : 0.0;

    int n = std::snprintf(
        buf, buf_size,
        "# HELP bytetaper_runtime_worker_enqueue_total Total number of jobs enqueued to the "
        "background worker queue.\n"
        "# TYPE bytetaper_runtime_worker_enqueue_total counter\n"
        "bytetaper_runtime_worker_enqueue_total %lu\n"
        "# HELP bytetaper_runtime_worker_enqueue_dropped_total Total number of jobs dropped due to "
        "queue saturation.\n"
        "# TYPE bytetaper_runtime_worker_enqueue_dropped_total counter\n"
        "bytetaper_runtime_worker_enqueue_dropped_total %lu\n"
        "# HELP bytetaper_runtime_worker_job_executed_total Total number of jobs successfully "
        "executed by workers.\n"
        "# TYPE bytetaper_runtime_worker_job_executed_total counter\n"
        "bytetaper_runtime_worker_job_executed_total %lu\n"
        "# HELP bytetaper_runtime_worker_job_error_total Total number of background jobs that "
        "failed during execution.\n"
        "# TYPE bytetaper_runtime_worker_job_error_total counter\n"
        "bytetaper_runtime_worker_job_error_total %lu\n"
        "# HELP bytetaper_runtime_worker_queue_depth Current number of jobs waiting in the worker "
        "queue.\n"
        "# TYPE bytetaper_runtime_worker_queue_depth gauge\n"
        "bytetaper_runtime_worker_queue_depth %lu\n"
        "# HELP bytetaper_runtime_worker_queue_capacity Maximum capacity of the background worker "
        "queue.\n"
        "# TYPE bytetaper_runtime_worker_queue_capacity gauge\n"
        "bytetaper_runtime_worker_queue_capacity %lu\n"
        "# HELP bytetaper_runtime_l2_async_lookup_total Total number of asynchronous L2 lookups "
        "attempted.\n"
        "# TYPE bytetaper_runtime_l2_async_lookup_total counter\n"
        "bytetaper_runtime_l2_async_lookup_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_lookup_hit_total Number of background L2 lookups that "
        "resulted in a cache hit.\n"
        "# TYPE bytetaper_runtime_l2_async_lookup_hit_total counter\n"
        "bytetaper_runtime_l2_async_lookup_hit_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_lookup_miss_total Number of background L2 lookups that "
        "resulted in a cache miss.\n"
        "# TYPE bytetaper_runtime_l2_async_lookup_miss_total counter\n"
        "bytetaper_runtime_l2_async_lookup_miss_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_lookup_error_total Number of background L2 lookups that "
        "failed due to storage errors.\n"
        "# TYPE bytetaper_runtime_l2_async_lookup_error_total counter\n"
        "bytetaper_runtime_l2_async_lookup_error_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_lookup_deduped_total Number of L2 lookups skipped "
        "because a lookup for the same key was already pending.\n"
        "# TYPE bytetaper_runtime_l2_async_lookup_deduped_total counter\n"
        "bytetaper_runtime_l2_async_lookup_deduped_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_store_total Total number of asynchronous L2 stores "
        "attempted.\n"
        "# TYPE bytetaper_runtime_l2_async_store_total counter\n"
        "bytetaper_runtime_l2_async_store_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_store_success_total Number of background L2 stores that "
        "completed successfully.\n"
        "# TYPE bytetaper_runtime_l2_async_store_success_total counter\n"
        "bytetaper_runtime_l2_async_store_success_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_store_error_total Number of background L2 stores that "
        "failed during persistence.\n"
        "# TYPE bytetaper_runtime_l2_async_store_error_total counter\n"
        "bytetaper_runtime_l2_async_store_error_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_store_dropped_total Number of L2 stores dropped due to "
        "queue saturation.\n"
        "# TYPE bytetaper_runtime_l2_async_store_dropped_total counter\n"
        "bytetaper_runtime_l2_async_store_dropped_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_store_oversized_skipped_total Number of L2 stores "
        "skipped because the response body exceeded the async size limit.\n"
        "# TYPE bytetaper_runtime_l2_async_store_oversized_skipped_total counter\n"
        "bytetaper_runtime_l2_async_store_oversized_skipped_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_store_encode_error_total Number of background L2 stores "
        "that failed due to encode errors.\n"
        "# TYPE bytetaper_runtime_l2_async_store_encode_error_total counter\n"
        "bytetaper_runtime_l2_async_store_encode_error_total %lu\n"
        "# HELP bytetaper_runtime_l2_async_store_body_too_large_total Number of background L2 "
        "stores "
        "that failed because the body was too large for the encode buffer.\n"
        "# TYPE bytetaper_runtime_l2_async_store_body_too_large_total counter\n"
        "bytetaper_runtime_l2_async_store_body_too_large_total %lu\n"
        "# HELP bytetaper_runtime_l2_to_l1_promotion_total Total number of successful cache "
        "promotions from L2 to L1.\n"
        "# TYPE bytetaper_runtime_l2_to_l1_promotion_total counter\n"
        "bytetaper_runtime_l2_to_l1_promotion_total %lu\n"
        "# HELP bytetaper_runtime_l2_to_l1_stale_rejected_total Number of L2-to-L1 promotions "
        "rejected because the L1 entry was fresher.\n"
        "# TYPE bytetaper_runtime_l2_to_l1_stale_rejected_total counter\n"
        "bytetaper_runtime_l2_to_l1_stale_rejected_total %lu\n"
        "# HELP bytetaper_runtime_l2_to_l1_promotion_skipped_body_too_large_total Number of "
        "L2-to-L1 "
        "promotions skipped because the body exceeded L1 max capacity.\n"
        "# TYPE bytetaper_runtime_l2_to_l1_promotion_skipped_body_too_large_total counter\n"
        "bytetaper_runtime_l2_to_l1_promotion_skipped_body_too_large_total %lu\n"
        "# HELP bytetaper_runtime_l2_lookup_body_too_large_for_buffer_total Number of "
        "L2 lookups that failed because the body was too large for the buffer.\n"
        "# TYPE bytetaper_runtime_l2_lookup_body_too_large_for_buffer_total counter\n"
        "bytetaper_runtime_l2_lookup_body_too_large_for_buffer_total %lu\n"
        "# HELP bytetaper_runtime_l2_lookup_expired_total Number of "
        "L2 lookups that found an expired entry.\n"
        "# TYPE bytetaper_runtime_l2_lookup_expired_total counter\n"
        "bytetaper_runtime_l2_lookup_expired_total %lu\n"
        "# HELP bytetaper_runtime_l2_lookup_decode_error_total Number of "
        "L2 lookups that failed to decode the entry.\n"
        "# TYPE bytetaper_runtime_l2_lookup_decode_error_total counter\n"
        "bytetaper_runtime_l2_lookup_decode_error_total %lu\n"
        "# HELP bytetaper_runtime_l2_lookup_rocksdb_error_total Number of "
        "L2 lookups that failed due to a RocksDB error.\n"
        "# TYPE bytetaper_runtime_l2_lookup_rocksdb_error_total counter\n"
        "bytetaper_runtime_l2_lookup_rocksdb_error_total %lu\n"
        "# HELP bytetaper_worker_store_body_pool_full_total Total number of store jobs dropped "
        "because the sharded store body pool was full.\n"
        "# TYPE bytetaper_worker_store_body_pool_full_total counter\n"
        "bytetaper_worker_store_body_pool_full_total %lu\n"
        "# HELP bytetaper_worker_lookup_lane_wait_ms_avg Average background worker queue "
        "wait time for L2 lookups.\n"
        "# TYPE bytetaper_worker_lookup_lane_wait_ms_avg gauge\n"
        "bytetaper_worker_lookup_lane_wait_ms_avg %.3f\n"
        "# HELP bytetaper_worker_store_lane_wait_ms_avg Average background worker queue "
        "wait time for L2 stores.\n"
        "# TYPE bytetaper_worker_store_lane_wait_ms_avg gauge\n"
        "bytetaper_worker_store_lane_wait_ms_avg %.3f\n"
        "# HELP bytetaper_worker_store_lane_starvation_total Total number of cycles where "
        "pending stores were starved of execution due to lookup lane quota.\n"
        "# TYPE bytetaper_worker_store_lane_starvation_total counter\n"
        "bytetaper_worker_store_lane_starvation_total %lu\n"
        "# HELP bytetaper_worker_store_body_pool_bytes_in_use Total memory bytes occupied "
        "by active stores in the body pools.\n"
        "# TYPE bytetaper_worker_store_body_pool_bytes_in_use gauge\n"
        "bytetaper_worker_store_body_pool_bytes_in_use %lu\n",
        metrics.worker_enqueue_total.load(std::memory_order_relaxed),
        metrics.worker_enqueue_dropped_total.load(std::memory_order_relaxed),
        metrics.worker_job_executed_total.load(std::memory_order_relaxed),
        metrics.worker_job_error_total.load(std::memory_order_relaxed),
        metrics.worker_queue_depth.load(std::memory_order_relaxed),
        metrics.worker_queue_capacity.load(std::memory_order_relaxed),
        metrics.l2_async_lookup_total.load(std::memory_order_relaxed),
        metrics.l2_async_lookup_hit_total.load(std::memory_order_relaxed),
        metrics.l2_async_lookup_miss_total.load(std::memory_order_relaxed),
        metrics.l2_async_lookup_error_total.load(std::memory_order_relaxed),
        metrics.l2_async_lookup_deduped_total.load(std::memory_order_relaxed),
        metrics.l2_async_store_total.load(std::memory_order_relaxed),
        metrics.l2_async_store_success_total.load(std::memory_order_relaxed),
        metrics.l2_async_store_error_total.load(std::memory_order_relaxed),
        metrics.l2_async_store_dropped_total.load(std::memory_order_relaxed),
        metrics.l2_async_store_oversized_skipped_total.load(std::memory_order_relaxed),
        metrics.l2_async_store_encode_error_total.load(std::memory_order_relaxed),
        metrics.l2_async_store_body_too_large_total.load(std::memory_order_relaxed),
        metrics.l2_to_l1_promotion_total.load(std::memory_order_relaxed),
        metrics.l2_to_l1_stale_rejected_total.load(std::memory_order_relaxed),
        metrics.l2_to_l1_promotion_skipped_body_too_large_total.load(std::memory_order_relaxed),
        metrics.l2_lookup_body_too_large_for_buffer_total.load(std::memory_order_relaxed),
        metrics.l2_lookup_expired_total.load(std::memory_order_relaxed),
        metrics.l2_lookup_decode_error_total.load(std::memory_order_relaxed),
        metrics.l2_lookup_rocksdb_error_total.load(std::memory_order_relaxed),
        metrics.worker_store_body_pool_full_total.load(std::memory_order_relaxed), lookup_wait_avg,
        store_wait_avg, metrics.worker_store_lane_starvation_total.load(std::memory_order_relaxed),
        metrics.worker_store_body_pool_bytes_in_use.load(std::memory_order_relaxed));

    if (n < 0 || static_cast<std::size_t>(n) >= buf_size) {
        return 0;
    }
    return static_cast<std::size_t>(n);
}

} // namespace bytetaper::metrics

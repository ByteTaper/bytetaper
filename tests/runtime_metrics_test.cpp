// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/runtime_metrics.h"
#include "runtime/worker_queue.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::metrics {

class RuntimeMetricsTest : public ::testing::Test {
protected:
    RuntimeMetrics metrics{};
};

TEST_F(RuntimeMetricsTest, RecordEventsIncrementsCounters) {
    record_runtime_event(&metrics, RuntimeMetricEvent::Enqueue);
    record_runtime_event(&metrics, RuntimeMetricEvent::EnqueueDropped);
    record_runtime_event(&metrics, RuntimeMetricEvent::JobExecuted);
    record_runtime_event(&metrics, RuntimeMetricEvent::JobError);

    EXPECT_EQ(metrics.worker_enqueue_total.load(), 1);
    EXPECT_EQ(metrics.worker_enqueue_dropped_total.load(), 1);
    EXPECT_EQ(metrics.worker_job_executed_total.load(), 1);
    EXPECT_EQ(metrics.worker_job_error_total.load(), 1);
}

TEST_F(RuntimeMetricsTest, L2LookupEvents) {
    record_runtime_event(&metrics, RuntimeMetricEvent::L2LookupEnqueued);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2LookupHit);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2LookupMiss);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2LookupError);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2LookupDeduped);

    EXPECT_EQ(metrics.l2_async_lookup_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_lookup_hit_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_lookup_miss_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_lookup_error_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_lookup_deduped_total.load(), 1);
}

TEST_F(RuntimeMetricsTest, L2StoreEvents) {
    record_runtime_event(&metrics, RuntimeMetricEvent::L2StoreEnqueued);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2StoreSuccess);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2StoreError);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2StoreDropped);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2StoreOversizedSkipped);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2StoreEncodeError);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2StoreBodyTooLarge);

    EXPECT_EQ(metrics.l2_async_store_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_store_success_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_store_error_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_store_dropped_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_store_oversized_skipped_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_store_encode_error_total.load(), 1);
    EXPECT_EQ(metrics.l2_async_store_body_too_large_total.load(), 1);
}

TEST_F(RuntimeMetricsTest, PromotionEvents) {
    record_runtime_event(&metrics, RuntimeMetricEvent::L2ToL1Promotion);
    record_runtime_event(&metrics, RuntimeMetricEvent::L2ToL1StaleRejected);

    EXPECT_EQ(metrics.l2_to_l1_promotion_total.load(), 1);
    EXPECT_EQ(metrics.l2_to_l1_stale_rejected_total.load(), 1);
}

TEST_F(RuntimeMetricsTest, RenderPrometheusFormat) {
    const std::size_t capacity = runtime::kRuntimeShardCount * runtime::kRuntimeQueueSlotsPerShard;

    metrics.worker_enqueue_total.store(10);
    metrics.worker_queue_depth.store(5);
    metrics.worker_queue_capacity.store(capacity);
    metrics.l2_async_lookup_hit_total.store(7);
    metrics.worker_store_body_pool_full_total.store(3);
    metrics.worker_lookup_lane_wait_ms_total.store(120);
    metrics.worker_lookup_lane_wait_count_total.store(4);
    metrics.worker_store_lane_wait_ms_total.store(240);
    metrics.worker_store_lane_wait_count_total.store(6);
    metrics.worker_store_lane_starvation_total.store(2);
    metrics.worker_store_body_pool_bytes_in_use.store(4096);

    char buf[8192];
    std::size_t len = render_runtime_metrics_prometheus(metrics, buf, sizeof(buf));
    ASSERT_GT(len, 0);

    EXPECT_NE(std::strstr(buf, "bytetaper_runtime_worker_enqueue_total 10"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_runtime_worker_queue_depth 5"), nullptr);

    std::string expected_capacity_str =
        "bytetaper_runtime_worker_queue_capacity " + std::to_string(capacity);
    EXPECT_NE(std::strstr(buf, expected_capacity_str.c_str()), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_runtime_l2_async_lookup_hit_total 7"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_worker_store_body_pool_full_total 3"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_worker_lookup_lane_wait_ms_avg 30.000"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_worker_store_lane_wait_ms_avg 40.000"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_worker_store_lane_starvation_total 2"), nullptr);
    EXPECT_NE(std::strstr(buf, "bytetaper_worker_store_body_pool_bytes_in_use 4096"), nullptr);

    // Check HELP/TYPE lines
    EXPECT_NE(std::strstr(buf, "# HELP bytetaper_runtime_worker_queue_depth"), nullptr);
    EXPECT_NE(std::strstr(buf, "# TYPE bytetaper_runtime_worker_queue_depth gauge"), nullptr);
    EXPECT_NE(std::strstr(buf, "# HELP bytetaper_worker_lookup_lane_wait_ms_avg"), nullptr);
    EXPECT_NE(std::strstr(buf, "# HELP bytetaper_worker_store_lane_starvation_total"), nullptr);
}

TEST_F(RuntimeMetricsTest, RenderOverflowSafety) {
    char small_buf[10];
    std::size_t len = render_runtime_metrics_prometheus(metrics, small_buf, sizeof(small_buf));
    EXPECT_EQ(len, 0);
}

TEST_F(RuntimeMetricsTest, NullRegistryIsSafe) {
    record_runtime_event(nullptr, RuntimeMetricEvent::Enqueue);
}

} // namespace bytetaper::metrics

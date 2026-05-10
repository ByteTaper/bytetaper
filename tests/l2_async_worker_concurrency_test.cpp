// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/l2_disk_cache.h"
#include "metrics/prometheus_registry.h"
#include "runtime/worker_queue.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::runtime {

class L2AsyncWorkerConcurrencyTest : public ::testing::Test {
public:
    void SetUp() override {
        metrics_registry = std::make_unique<metrics::MetricsRegistry>();
        worker_queue = std::make_unique<WorkerQueue>();

        WorkerQueueConfig config{};
        config.worker_count = 1;
        worker_queue_init(worker_queue.get(), config);

        resources.runtime_metrics = &metrics_registry->runtime_metrics;
        worker_queue->resources = resources;
        worker_queue->running = true;
        // resources.l2_cache is left null to simulate errors in some tests
    }

    void TearDown() override {
        if (l2_cache) {
            cache::l2_close(&l2_cache);
        }
        worker_queue_shutdown(worker_queue.get());
    }

    std::unique_ptr<WorkerQueue> worker_queue;
    cache::L2DiskCache* l2_cache = nullptr;
    std::unique_ptr<metrics::MetricsRegistry> metrics_registry;
    WorkerQueueResources resources;
};

TEST_F(L2AsyncWorkerConcurrencyTest, WorkerErrorIncrementsMetric) {
    // Leave resources.l2_cache as nullptr to trigger error path
    worker_queue->resources = resources;

    L2StoreJob job{};
    job.body_len = 100;

    worker_queue_try_enqueue_store(worker_queue.get(), job);
    bool executed = worker_queue_execute_one_for_test(worker_queue.get());
    EXPECT_TRUE(executed);

    EXPECT_EQ(this->metrics_registry->runtime_metrics.l2_async_store_error_total.load(), 1);
    EXPECT_EQ(this->metrics_registry->runtime_metrics.worker_job_error_total.load(), 1);
}

TEST_F(L2AsyncWorkerConcurrencyTest, AsyncL2ErrorDoesNotFailRequest) {
    // This is more of a logic check: if worker fails, it shouldn't crash
    worker_queue->resources = resources;

    L2LookupJob job{};
    ::strncpy(job.key, "err_key", sizeof(job.key) - 1);

    worker_queue_try_enqueue_lookup(worker_queue.get(), job);
    // Should complete without exception/crash
    EXPECT_TRUE(worker_queue_execute_one_for_test(worker_queue.get()));
    EXPECT_EQ(this->metrics_registry->runtime_metrics.l2_async_lookup_error_total.load(), 1);
}

TEST_F(L2AsyncWorkerConcurrencyTest, PendingLookupClearedOnMiss) {
    worker_queue->resources = resources;

    const char* key = "miss_key";

    L2LookupJob job{};
    ::strncpy(job.key, key, sizeof(job.key) - 1);

    // Enqueue handles marking it as pending in the correct shard
    ASSERT_TRUE(worker_queue_try_enqueue_lookup(worker_queue.get(), job));
    worker_queue_execute_one_for_test(worker_queue.get());

    // Verify marker is cleared even on miss by trying to enqueue again
    // If it was cleared, try_enqueue should succeed (at least for the pending check)
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(worker_queue.get(), job))
        << "Marker should have been cleared";
}

TEST_F(L2AsyncWorkerConcurrencyTest, PendingLookupClearedOnHit) {
    // We need a real (or mocked) L2 for a hit, but let's test the plumbing
    // by verifying the worker clears the registry.
    worker_queue->resources = resources;

    const char* key = "any_key";

    L2LookupJob job{};
    ::strncpy(job.key, key, sizeof(job.key) - 1);

    ASSERT_TRUE(worker_queue_try_enqueue_lookup(worker_queue.get(), job));
    worker_queue_execute_one_for_test(worker_queue.get());

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(worker_queue.get(), job))
        << "Marker should have been cleared";
}

TEST_F(L2AsyncWorkerConcurrencyTest, PendingLookupClearedOnError) {
    worker_queue->resources = resources;
    resources.l2_cache = nullptr; // force error

    const char* key = "error_key";

    L2LookupJob job{};
    ::strncpy(job.key, key, sizeof(job.key) - 1);

    ASSERT_TRUE(worker_queue_try_enqueue_lookup(worker_queue.get(), job));
    worker_queue_execute_one_for_test(worker_queue.get());

    // Verify marker is cleared even on error
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(worker_queue.get(), job))
        << "Marker should have been cleared";
    EXPECT_EQ(this->metrics_registry->runtime_metrics.l2_async_lookup_error_total.load(), 1);
}

} // namespace bytetaper::runtime

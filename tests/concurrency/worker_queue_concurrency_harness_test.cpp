// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "concurrency_test_barrier.h"
#include "metrics/prometheus_registry.h"
#include "runtime/worker_queue.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace bytetaper::runtime {

class WorkerQueueConcurrencyHarnessTest : public ::testing::Test {
protected:
    void SetUp() override {
        worker_queue = std::make_unique<WorkerQueue>();
        metrics_reg = std::make_unique<metrics::MetricsRegistry>();

        WorkerQueueConfig config{};
        config.worker_count = 2;
        config.lookup_lane_quota = 2;
        config.store_lane_quota = 2;
        config.invalidate_lane_quota = 2;
        const char* err = worker_queue_init(worker_queue.get(), config);
        EXPECT_EQ(err, nullptr);

        resources.l1_cache = nullptr;
        resources.runtime_metrics = &metrics_reg->runtime_metrics;
    }

    void TearDown() override {
        worker_queue_shutdown(worker_queue.get());
    }

    std::unique_ptr<WorkerQueue> worker_queue;
    std::unique_ptr<metrics::MetricsRegistry> metrics_reg;
    WorkerQueueResources resources;
};

TEST_F(WorkerQueueConcurrencyHarnessTest, ConcurrentEnqueueAndShutdown) {
    worker_queue_start(worker_queue.get(), resources);

    constexpr int kProducers = 4;
    std::vector<std::thread> threads;
    StartGate gate;
    std::atomic<bool> run{ true };

    std::mutex progress_mu;
    std::condition_variable progress_cv;
    std::atomic<int> successful_enqueues{ 0 };

    for (int i = 0; i < kProducers; ++i) {
        threads.emplace_back([this, i, &gate, &run, &progress_mu, &progress_cv,
                              &successful_enqueues] {
            gate.arrive_and_wait(kProducers); // Only producers in the arrive barrier

            int seq = 0;
            while (run.load(std::memory_order_acquire)) {
                bool success = false;
                std::string key = "key_" + std::to_string(i) + "_" + std::to_string(seq++);
                if (i == 0) {
                    L2LookupJob job{};
                    ::strncpy(job.key, key.c_str(), sizeof(job.key) - 1);
                    success = worker_queue_try_enqueue_lookup(worker_queue.get(), job);
                } else if (i == 1) {
                    L2StoreJob job{};
                    ::strncpy(job.key, key.c_str(), sizeof(job.key) - 1);
                    job.entry.body = "data";
                    job.body_len = 4;
                    success = worker_queue_try_enqueue_store(worker_queue.get(), job);
                } else if (i == 2) {
                    success =
                        worker_queue_enqueue_l2_invalidate(worker_queue.get(), key.c_str(), 123456);
                } else {
                    int r = seq % 3;
                    if (r == 0) {
                        L2LookupJob job{};
                        ::strncpy(job.key, key.c_str(), sizeof(job.key) - 1);
                        success = worker_queue_try_enqueue_lookup(worker_queue.get(), job);
                    } else if (r == 1) {
                        L2StoreJob job{};
                        ::strncpy(job.key, key.c_str(), sizeof(job.key) - 1);
                        job.entry.body = "data";
                        job.body_len = 4;
                        success = worker_queue_try_enqueue_store(worker_queue.get(), job);
                    } else {
                        success = worker_queue_enqueue_l2_invalidate(worker_queue.get(),
                                                                     key.c_str(), 123456);
                    }
                }

                if (success) {
                    int prev = successful_enqueues.fetch_add(1, std::memory_order_acq_rel);
                    if (prev + 1 >= 100) {
                        std::lock_guard<std::mutex> lock(progress_mu);
                        progress_cv.notify_one();
                    }
                }
                std::this_thread::yield();
            }
        });
    }

    // Arrive and start racing
    gate.wait_for_ready(kProducers);
    gate.release();

    // Wait until at least 100 successful enqueues are made, bounded to 5 seconds
    {
        std::unique_lock<std::mutex> lock(progress_mu);
        bool progress_ok = progress_cv.wait_for(lock, std::chrono::seconds(5), [&] {
            return successful_enqueues.load(std::memory_order_acquire) >= 100;
        });
        ASSERT_TRUE(progress_ok)
            << "Timed out waiting for 100 successful enqueues. Progress stalled!";
    }

    // Call shutdown concurrently while producers are still racing
    worker_queue_shutdown(worker_queue.get());

    // Stop producers and join them
    run.store(false, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }

    // Assert shutdown completed, running is false, and memory bytes in use is 0
    EXPECT_FALSE(worker_queue->running);
    EXPECT_EQ(worker_queue->store_body_pool_bytes_in_use.load(std::memory_order_relaxed), 0);
}

TEST_F(WorkerQueueConcurrencyHarnessTest, EnqueueAfterShutdownReturnsFalse) {
    worker_queue_start(worker_queue.get(), resources);
    worker_queue_shutdown(worker_queue.get());

    L2LookupJob job{};
    EXPECT_FALSE(worker_queue_try_enqueue_lookup(worker_queue.get(), job));
}

} // namespace bytetaper::runtime

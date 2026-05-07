// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/follower_wait_pool.h"
#include "coalescing/inflight_registry.h"
#include "metrics/coalescing_metrics.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace bytetaper::coalescing {

TEST(CoalescingFollowerWaitPoolTest, BasicStartupShutdown) {
    FollowerWaitPool pool{};
    FollowerWaitPoolConfig cfg{};
    cfg.worker_count = 2;
    cfg.queue_capacity = 4;

    const char* err = follower_wait_pool_init(&pool, cfg);
    EXPECT_EQ(err, nullptr);

    auto registry = std::make_unique<InFlightRegistry>();
    registry_init(registry.get());

    metrics::CoalescingMetrics metrics{};
    FollowerWaitPoolResources res{ registry.get(), &metrics };

    err = follower_wait_pool_start(&pool, res);
    EXPECT_EQ(err, nullptr);

    follower_wait_pool_shutdown(&pool);
}

TEST(CoalescingFollowerWaitPoolTest, SuccessfulWaitCoalescedResponse) {
    FollowerWaitPool pool{};
    FollowerWaitPoolConfig cfg{};
    cfg.worker_count = 1;
    cfg.queue_capacity = 10;
    follower_wait_pool_init(&pool, cfg);

    auto registry = std::make_unique<InFlightRegistry>();
    registry_init(registry.get());
    metrics::CoalescingMetrics metrics{};
    follower_wait_pool_start(&pool, { registry.get(), &metrics });

    // Register a key as leader first so we are in-flight
    auto reg_res = registry_register(registry.get(), "test-key-1", 1000, 100, 128);
    EXPECT_EQ(reg_res.role, InFlightRole::Leader);

    // Now submit the follower wait job asynchronously to avoid blocking the main thread
    RegistrySharedResponseOutput out{};
    RegistryWaitResult result = RegistryWaitResult::Missing;

    std::thread sub_thread([&]() {
        bool ok = follower_wait_pool_submit_and_wait(&pool, "test-key-1", 1000, 0, &out, &result);
        EXPECT_TRUE(ok);
    });

    // Wait slightly to ensure follower is enqueued and waiting in the pool worker thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Complete the key as leader with a payload
    registry_complete_with_response(registry.get(), "test-key-1", 200, "application/json",
                                    "{\"status\":\"ok\"}", 15, 1050);

    // Join sub_thread
    sub_thread.join();

    EXPECT_EQ(result, RegistryWaitResult::SharedResponseReady);
    EXPECT_EQ(out.status_code, 200);
    EXPECT_STREQ(out.content_type, "application/json");
    EXPECT_STREQ(out.body, "{\"status\":\"ok\"}");
    EXPECT_EQ(out.body_len, 15);

    follower_wait_pool_shutdown(&pool);
}

TEST(CoalescingFollowerWaitPoolTest, QueueFullSafety) {
    FollowerWaitPool pool{};
    FollowerWaitPoolConfig cfg{};
    cfg.worker_count = 0; // No workers to pull from queue!
    cfg.queue_capacity = 2;
    follower_wait_pool_init(&pool, cfg);

    auto registry = std::make_unique<InFlightRegistry>();
    registry_init(registry.get());
    metrics::CoalescingMetrics metrics{};
    follower_wait_pool_start(&pool, { registry.get(), &metrics });

    RegistrySharedResponseOutput out{};
    RegistryWaitResult result = RegistryWaitResult::Missing;

    // First submission should enqueue (since queue is empty and size 0 < 2)
    // We run it on a thread because it blocks (no workers to dequeue it)
    std::thread t1(
        [&]() { follower_wait_pool_submit_and_wait(&pool, "key1", 500, 0, &out, &result); });

    // Wait slightly to let t1 enqueue
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Second submission should also enqueue (queue size 1 < 2)
    std::thread t2(
        [&]() { follower_wait_pool_submit_and_wait(&pool, "key2", 500, 0, &out, &result); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Third submission should fail immediately because queue size (2) is at capacity (2)
    bool ok = follower_wait_pool_submit_and_wait(&pool, "key3", 500, 0, &out, &result);
    EXPECT_FALSE(ok); // Safe fast-path rejection!

    // Shutdown pool to wake up pending enqueued items and join threads
    follower_wait_pool_shutdown(&pool);
    t1.join();
    t2.join();
}

} // namespace bytetaper::coalescing

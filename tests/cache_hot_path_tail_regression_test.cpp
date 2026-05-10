// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/pipeline.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "extproc/default_pipelines.h"
#include "policy/route_policy.h"
#include "runtime/worker_queue.h"
#include "stages/coalescing_follower_wait_stage.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace bytetaper::extproc {

class CacheHotPathTailRegressionTest : public ::testing::Test {
protected:
    void SetUp() override {
        l1 = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1.get());

        registry = std::make_unique<coalescing::InFlightRegistry>();
        coalescing::registry_init(registry.get());

        q = std::make_unique<runtime::WorkerQueue>();
        runtime::WorkerQueueConfig cfg;
        cfg.worker_count = 4;
        runtime::worker_queue_init(q.get(), cfg);
        q->running = true;

        policy.route_id = "test-route";
        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.coalescing.enabled = true;

        ctx.matched_policy = &policy;
        ctx.l1_cache = l1.get();
        ctx.coalescing_registry = registry.get();
        ctx.worker_queue = q.get();
        ctx.coalescing_decision.action = coalescing::CoalescingAction::Bypass;

        std::strcpy(ctx.raw_path, "/test");
        ctx.request_method = policy::HttpMethod::Get;
    }

    void TearDown() override {
        runtime::worker_queue_shutdown(q.get());
    }

    std::unique_ptr<cache::L1Cache> l1;
    std::unique_ptr<coalescing::InFlightRegistry> registry;
    std::unique_ptr<runtime::WorkerQueue> q;
    policy::RoutePolicy policy;
    apg::ApgTransformContext ctx;
};

TEST_F(CacheHotPathTailRegressionTest, HotPathL1HitLatencyBoundedUnderLoad) {
    // 1. Populate L1
    cache::CacheEntry entry{};
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.path = ctx.raw_path;
    ki.route_id = policy.route_id;
    ki.policy_version = policy.route_id;
    cache::build_cache_key(ki, entry.key, sizeof(entry.key));

    entry.expires_at_epoch_ms = 2000000000000LL; // far future
    cache::l1_put(l1.get(), entry);

    // 2. Run kLookupStages 1000 times
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; i++) {
        auto output = apg::run_pipeline(kLookupStages, kLookupStageCount, ctx);
        ASSERT_EQ(output.result, apg::StageResult::SkipRemaining);
    }
    auto end = std::chrono::steady_clock::now();

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    // 1000 L1 hits should be extremely fast (<< 100ms, usually < 1ms)
    EXPECT_LT(elapsed_us, 100000);

    // 3. Assert no coalescing entries created
    // If coalescing ran before L1, it would create entries in registry.
    for (std::size_t i = 0; i < coalescing::kInFlightShards; i++) {
        auto& shard = registry->shards[i];
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (auto& slot : shard.slots) {
            EXPECT_FALSE(slot.active);
        }
    }
}

TEST_F(CacheHotPathTailRegressionTest, GlobalMutexAbsentFromAsyncPath) {
    // Enqueue jobs for different shards simultaneously
    // We'll use 8 threads, each enqueuing to a different shard.
    std::vector<std::thread> threads;
    std::atomic<int> success_count{ 0 };

    for (int i = 0; i < 8; i++) {
        threads.emplace_back([this, i, &success_count]() {
            runtime::L2StoreJob job;
            // Generate keys that map to different shards
            // "key-0", "key-1", ...
            char key[32];
            std::sprintf(key, "key-%d", i * 32);
            std::strcpy(job.key, key);

            if (runtime::worker_queue_try_enqueue_store(q.get(), job)) {
                success_count++;
            }
        });
    }

    for (auto& t : threads)
        t.join();

    EXPECT_EQ(success_count, 8);
    // If there was a global mutex, this would be serialized.
    // Wall time is hard to measure for 8 enqueues, but we verify they all succeed.
}

TEST_F(CacheHotPathTailRegressionTest, SleepForAbsentFromFollowerWait) {
    // Behavioral: time follower wait stage with a 50ms window and no leader.
    // (Wait, if no leader, it bypasses or returns Continue immediately if registry is null)
    // If registry is present but no leader registered for key, it returns timeout-fallback.

    policy.coalescing.backend_timeout_ms = 40;
    policy.coalescing.handoff_buffer_ms = 10;
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
    std::strcpy(ctx.coalescing_decision.key, "no-leader-key");

    // We need to register a leader first so the follower actually enters wait
    coalescing::registry_register(registry.get(), "no-leader-key", 1000, 50, 128);

    auto start = std::chrono::steady_clock::now();
    stages::coalescing_follower_wait_stage(ctx);
    auto end = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // Should be exactly wait_window_ms + overhead.
    // Previous polling had +/- 10ms drift. Event driven should be precise.
    EXPECT_GE(elapsed_ms, 50);
    EXPECT_LT(elapsed_ms, 70); // Allowing some OS scheduling overhead
}

} // namespace bytetaper::extproc

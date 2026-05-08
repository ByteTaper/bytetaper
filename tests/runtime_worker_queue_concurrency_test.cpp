// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/pipeline.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "metrics/prometheus_registry.h"
#include "runtime/worker_queue.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_decision_stage.h"
#include "stages/coalescing_follower_wait_stage.h"
#include "stages/coalescing_leader_completion_stage.h"
#include "stages/l1_cache_store_stage.h"
#include "stages/l2_cache_async_store_enqueue_stage.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

namespace bytetaper::runtime {

class WorkerQueueConcurrencyTest : public ::testing::Test {
public:
    void SetUp() override {
        l1_cache = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1_cache.get());
        metrics_reg = std::make_unique<metrics::MetricsRegistry>();

        WorkerQueueConfig config{};
        config.worker_count = 2;
        worker_queue_init(&worker_queue, config);

        resources.l1_cache = l1_cache.get();
        resources.runtime_metrics = &metrics_reg->runtime_metrics;
    }

    void TearDown() override {
        worker_queue_shutdown(&worker_queue);
    }

    WorkerQueue worker_queue;
    std::unique_ptr<cache::L1Cache> l1_cache;
    std::unique_ptr<metrics::MetricsRegistry> metrics_reg;
    WorkerQueueResources resources;
};

TEST_F(WorkerQueueConcurrencyTest, ShutdownDoesNotHang) {
    worker_queue_start(&worker_queue, resources);

    // Enqueue some work
    for (int i = 0; i < 4; ++i) {
        L2LookupJob job{};
        std::string key = "key" + std::to_string(i);
        ::strncpy(job.key, key.c_str(), sizeof(job.key) - 1);
        worker_queue_try_enqueue_lookup(&worker_queue, job);
    }

    // Shutdown should join threads and complete
    // GTest will timeout if it hangs
    worker_queue_shutdown(&worker_queue);
    EXPECT_FALSE(worker_queue.running);
}

TEST_F(WorkerQueueConcurrencyTest, ShutdownStopsEnqueueBeforeJoin) {
    worker_queue_start(&worker_queue, resources);
    worker_queue_shutdown(&worker_queue);

    L2LookupJob job{};
    EXPECT_FALSE(worker_queue_try_enqueue_lookup(&worker_queue, job));
}

TEST_F(WorkerQueueConcurrencyTest, WorkerDoesNotReadExpiredRequestPointer) {
    // This test verifies that the worker operates on the slot-owned body,
    // not a pointer to the original source buffer that might have gone out of scope.

    char source_body[100];
    std::memset(source_body, 0xAA, sizeof(source_body));

    L2StoreJob job{};
    job.entry.body = source_body;
    job.body_len = sizeof(source_body);

    worker_queue_start(&worker_queue, resources);
    worker_queue_try_enqueue_store(&worker_queue, job);

    // Simulate source buffer being overwritten/reused
    std::memset(source_body, 0xBB, sizeof(source_body));

    // Let's just verify the slot copy directly as a proxy for "does not read expired pointer"
    {
        bool found = false;
        for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
            std::lock_guard<std::mutex> lock(worker_queue.shards[i].mu);
            if (worker_queue.shards[i].store_count > 0) {
                std::uint32_t body_slot =
                    worker_queue.shards[i].store_slots[worker_queue.shards[i].store_head].body_slot;
                EXPECT_EQ(worker_queue.shards[i].body_pool.heap_bodies[body_slot][0], (char) 0xAA);
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }
}

TEST_F(WorkerQueueConcurrencyTest, L1StoreBeforeLeaderCompletion) {
    apg::ApgTransformContext ctx{};
    ctx.l1_cache = this->l1_cache.get();
    ctx.cache_metrics = &this->metrics_reg->cache_metrics;
    ctx.coalescing_metrics = &this->metrics_reg->coalescing_metrics;
    ctx.runtime_metrics = &this->metrics_reg->runtime_metrics;
    ctx.worker_queue = &this->worker_queue;
    ctx.response_status_code = 200;
    ctx.request_epoch_ms = 1000;

    policy::RoutePolicy policy{};
    policy.route_id = "test_route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.cache.ttl_seconds = 60;

    ctx.matched_policy = &policy;
    ::strncpy(ctx.raw_path, "/test", sizeof(ctx.raw_path) - 1);
    ctx.response_body = "test_body";
    ctx.response_body_len = 9;

    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());
    ctx.coalescing_registry = registry.get();

    // Mark as leader
    const char* shared_key = "test_key";
    coalescing::registry_register(registry.get(), shared_key, 100, 1000, 10);
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Leader;
    ::strncpy(ctx.coalescing_decision.key, shared_key, sizeof(ctx.coalescing_decision.key) - 1);

    const apg::ApgStage kStages[] = { stages::cache_key_prepare_stage, stages::l1_cache_store_stage,
                                      stages::coalescing_leader_completion_stage };

    apg::run_pipeline(kStages, 3, ctx);

    // Build the key to check L1
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    cache::build_cache_key(ki, key_buf, sizeof(key_buf));

    cache::CacheEntry hit{};
    char body_buf[cache::kL1MaxBodySize];
    EXPECT_TRUE(cache::l1_get(this->l1_cache.get(), key_buf, 0, &hit, body_buf, sizeof(body_buf)));
    EXPECT_STREQ((const char*) hit.body, "test_body");
}

TEST_F(WorkerQueueConcurrencyTest, FollowerCanReadL1AfterLeaderCompletion) {
    apg::ApgTransformContext leader_ctx{};
    leader_ctx.l1_cache = this->l1_cache.get();
    leader_ctx.cache_metrics = &this->metrics_reg->cache_metrics;
    leader_ctx.coalescing_metrics = &this->metrics_reg->coalescing_metrics;
    leader_ctx.runtime_metrics = &this->metrics_reg->runtime_metrics;
    leader_ctx.worker_queue = &this->worker_queue;
    leader_ctx.response_status_code = 200;
    leader_ctx.request_epoch_ms = 1000;

    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());
    leader_ctx.coalescing_registry = registry.get();

    policy::RoutePolicy policy{};
    policy.route_id = "test_route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.cache.ttl_seconds = 60;
    leader_ctx.matched_policy = &policy;
    ::strncpy(leader_ctx.raw_path, "/shared", sizeof(leader_ctx.raw_path) - 1);
    leader_ctx.response_body = "leader_data";
    leader_ctx.response_body_len = 11;

    // Leader starts
    const char* shared_key = "shared_key";
    coalescing::registry_register(registry.get(), shared_key, 100, 1000, 10);
    leader_ctx.coalescing_decision.action = coalescing::CoalescingAction::Leader;
    ::strncpy(leader_ctx.coalescing_decision.key, shared_key,
              sizeof(leader_ctx.coalescing_decision.key) - 1);

    // Leader stores and completes
    const apg::ApgStage kLeaderStages[] = { stages::cache_key_prepare_stage,
                                            stages::l1_cache_store_stage,
                                            stages::l2_cache_async_store_enqueue_stage,
                                            stages::coalescing_leader_completion_stage };
    apg::run_pipeline(kLeaderStages, 4, leader_ctx);

    // Follower starts
    apg::ApgTransformContext follower_ctx{};
    follower_ctx.l1_cache = this->l1_cache.get();
    follower_ctx.cache_metrics = &this->metrics_reg->cache_metrics;
    follower_ctx.coalescing_metrics = &this->metrics_reg->coalescing_metrics;
    follower_ctx.coalescing_registry = registry.get();
    follower_ctx.matched_policy = &policy;
    ::strncpy(follower_ctx.raw_path, "/shared", sizeof(follower_ctx.raw_path) - 1);

    follower_ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
    ::strncpy(follower_ctx.coalescing_decision.key, shared_key,
              sizeof(follower_ctx.coalescing_decision.key) - 1);

    // Follower wait stage should see L1 hit immediately
    stages::cache_key_prepare_stage(follower_ctx);
    apg::StageOutput output = stages::coalescing_follower_wait_stage(follower_ctx);
    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);
    EXPECT_TRUE(follower_ctx.cache_hit);
}

} // namespace bytetaper::runtime

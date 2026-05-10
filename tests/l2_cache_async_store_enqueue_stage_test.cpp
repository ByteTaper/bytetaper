// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "cache/cache_entry.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/inflight_registry.h"
#include "hash/hash.h"
#include "metrics/runtime_metrics.h"
#include "runtime/worker_queue.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/l2_cache_async_store_enqueue_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

class L2CacheAsyncStoreEnqueueStageTest : public ::testing::Test {
protected:
    void SetUp() override {
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
        l1_cache = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1_cache.get());

        // L2 is needed for stage to run, but we don't need real RocksDB for these logic tests
        l2_cache = reinterpret_cast<cache::L2DiskCache*>(0x1234);

        runtime::WorkerQueueConfig wq_config{};
        wq_config.worker_count = 1;
        runtime::worker_queue_init(&worker_queue, wq_config);
        worker_queue.running = true;

        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.cache.ttl_seconds = 60;
        policy.route_id = "test_route";

        ctx.matched_policy = &policy;
        ctx.l1_cache = l1_cache.get();
        ctx.l2_cache = l2_cache;
        ctx.worker_queue = &worker_queue;
        ctx.runtime_metrics = &metrics;
        std::strcpy(ctx.raw_path, "/path");
        ctx.request_method = policy::HttpMethod::Get;
        ctx.response_status_code = 200;
        ctx.response_body = "hello";
        ctx.response_body_len = 5;
    }

    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    std::unique_ptr<cache::L1Cache> l1_cache;
    cache::L2DiskCache* l2_cache;
    runtime::WorkerQueue worker_queue;
    metrics::RuntimeMetrics metrics{};
    policy::RoutePolicy policy;
    apg::ApgTransformContext ctx;
};

TEST_F(L2CacheAsyncStoreEnqueueStageTest, KeyNotReadyReturnsContinue) {
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "key-not-ready");
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, EligibleResponseEnqueuesL2Store) {
    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "enqueued");
    std::size_t total_count = 0;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        total_count += worker_queue.shards[i].store_count;
    }
    EXPECT_EQ(total_count, 1u);
    EXPECT_EQ(metrics.l2_async_store_total.load(), 1);

    // Verify job content in whichever shard it landed
    bool found = false;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        if (worker_queue.shards[i].store_count > 0) {
            auto& slot = worker_queue.shards[i].store_slots[worker_queue.shards[i].store_head];
            std::uint32_t body_slot = slot.body_slot;
            EXPECT_STREQ(worker_queue.shards[i].body_pool.heap_bodies[body_slot], "hello");
            EXPECT_EQ(slot.body_len, 5u);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, L2StoreQueueFullDoesNotFailResponse) {
    // Build the actual key the stage will use
    char actual_key[cache::kCacheKeyMaxLen];
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    cache::build_cache_key(ki, actual_key, sizeof(actual_key));

    // Find shard for actual_key
    std::uint32_t shard_idx = static_cast<std::uint32_t>(
        bytetaper::hash::hash_cstr_runtime(actual_key) % runtime::kRuntimeShardCount);

    // Directly saturate store queue for that shard
    worker_queue.shards[shard_idx].store_count = runtime::kRuntimeQueueSlotsPerShard;

    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "queue-full");
    EXPECT_EQ(metrics.l2_async_store_dropped_total.load(), 1);
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, L2StoreJobOwnsBodyMemory) {
    char mutable_body[] = "world";
    ctx.response_body = mutable_body;
    ctx.response_body_len = 5;

    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);

    // Overwrite original buffer
    std::memset(mutable_body, 0, 5);

    // Verify slot body is unchanged in whichever shard it landed
    bool found = false;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        if (worker_queue.shards[i].store_count > 0) {
            auto& slot = worker_queue.shards[i].store_slots[worker_queue.shards[i].store_head];
            std::uint32_t body_slot = slot.body_slot;
            EXPECT_STREQ(worker_queue.shards[i].body_pool.heap_bodies[body_slot], "world");
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, Large65KBBodyEnqueuesSuccessfully) {
    ctx.response_body_len = runtime::kAsyncL2LookupScratchSize + 1;
    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "enqueued");
    EXPECT_EQ(metrics.l2_async_store_total.load(), 1);
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, CoalescingHandoffEnabledForMediumBody) {
    auto test_registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(test_registry.get());

    ctx.coalescing_registry = test_registry.get();
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Leader;
    std::strcpy(ctx.coalescing_decision.key, "test-coalescing-key");
    ctx.coalescing_decision.lifecycle_generation = 123;

    // Medium body is > kL1MaxBodySize (3072) and <= kL2BodyBufSize (65536)
    ctx.response_body_len = cache::kL1MaxBodySize + 100;
    std::string medium_body(ctx.response_body_len, 'x');
    ctx.response_body = medium_body.c_str();

    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "enqueued");

    // Find the enqueued job and assert
    bool found = false;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        if (worker_queue.shards[i].store_count > 0) {
            auto& slot = worker_queue.shards[i].store_slots[worker_queue.shards[i].store_head];
            EXPECT_TRUE(slot.coalescing_handoff_enabled);
            EXPECT_EQ(slot.coalescing_registry, test_registry.get());
            EXPECT_STREQ(slot.coalescing_key, "test-coalescing-key");
            EXPECT_EQ(slot.lifecycle_generation, 123u);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, CoalescingHandoffDisabledForLargeBody) {
    auto test_registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(test_registry.get());

    ctx.coalescing_registry = test_registry.get();
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Leader;
    std::strcpy(ctx.coalescing_decision.key, "test-coalescing-key");
    ctx.coalescing_decision.lifecycle_generation = 456;

    // Large body is > kL2BodyBufSize (65536) and <= kL2MaxBodySize (1048576)
    ctx.response_body_len = apg::ApgTransformContext::kL2BodyBufSize + 100;
    std::string large_body(ctx.response_body_len, 'y');
    ctx.response_body = large_body.c_str();

    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "enqueued");

    // Find the enqueued job and assert
    bool found = false;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        if (worker_queue.shards[i].store_count > 0) {
            auto& slot = worker_queue.shards[i].store_slots[worker_queue.shards[i].store_head];
            EXPECT_FALSE(slot.coalescing_handoff_enabled);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, OversizedBodySkipsAsyncL2Store) {
    ctx.response_body_len = runtime::kAsyncL2StoreMaxBodySize + 1;
    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "body-too-large");
    EXPECT_EQ(metrics.l2_async_store_oversized_skipped_total.load(), 1);
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, NonGetRequestSkips) {
    ctx.request_method = policy::HttpMethod::Post;
    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "non-get");
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, Non2xxStatusSkips) {
    ctx.response_status_code = 500;
    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "non-2xx");
}

TEST_F(L2CacheAsyncStoreEnqueueStageTest, NoTtlSkips) {
    policy.cache.ttl_seconds = 0;
    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_store_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "no-ttl");
}

} // namespace bytetaper::stages

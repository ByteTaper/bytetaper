// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "hash/hash.h"
#include "metrics/runtime_metrics.h"
#include "runtime/route_cache_epoch_store.h"
#include "runtime/worker_queue.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/l2_cache_async_lookup_enqueue_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

class L2CacheAsyncLookupEnqueueStageTest : public ::testing::Test {
protected:
    void SetUp() override {
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
        l1_cache = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1_cache.get());

        l2_cache = reinterpret_cast<cache::L2DiskCache*>(0x1234);
        worker_queue = std::make_unique<runtime::WorkerQueue>();

        runtime::WorkerQueueConfig wq_config{};
        wq_config.worker_count = 1;
        runtime::worker_queue_init(worker_queue.get(), wq_config);
        worker_queue->running = true;

        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.route_id = "test_route";

        store.count = 0;
        runtime::route_cache_epoch_register(&store, policy.route_id);

        ctx.matched_policy = &policy;
        ctx.l1_cache = l1_cache.get();
        ctx.l2_cache = l2_cache;
        ctx.worker_queue = worker_queue.get();
        ctx.route_cache_epoch_store = &store;
        ctx.runtime_metrics = &metrics;
        std::strcpy(ctx.raw_path, "/path");
        ctx.request_method = policy::HttpMethod::Get;
    }

    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
        runtime::worker_queue_shutdown(worker_queue.get());
    }

    std::unique_ptr<cache::L1Cache> l1_cache;
    cache::L2DiskCache* l2_cache;
    std::unique_ptr<runtime::WorkerQueue> worker_queue;
    runtime::RouteCacheEpochStore store;
    metrics::RuntimeMetrics metrics{};
    policy::RoutePolicy policy;
    apg::ApgTransformContext ctx;
};

TEST_F(L2CacheAsyncLookupEnqueueStageTest, KeyNotReadyReturnsContinue) {
    ctx.cache_hit = false;
    auto output = l2_cache_async_lookup_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "key-not-ready");
}

TEST_F(L2CacheAsyncLookupEnqueueStageTest, L1HitSkipsEnqueue) {
    ctx.cache_hit = true;
    auto output = l2_cache_async_lookup_enqueue_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "l1-hit-skip");
    std::size_t total_count = 0;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        total_count += worker_queue->shards[i].lookup_count;
    }
    EXPECT_EQ(total_count, 0u);
}

TEST_F(L2CacheAsyncLookupEnqueueStageTest, L1MissEnqueuesAndContinues) {
    ctx.cache_hit = false;
    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_lookup_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "enqueued");
    std::size_t total_count = 0;
    std::size_t pending_count = 0;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        total_count += worker_queue->shards[i].lookup_count;
        pending_count += worker_queue->shards[i].pending_count;
    }
    EXPECT_EQ(total_count, 1u);
    EXPECT_EQ(pending_count, 1u);
    EXPECT_EQ(metrics.l2_async_lookup_total.load(), 1);
}

TEST_F(L2CacheAsyncLookupEnqueueStageTest, DuplicateKeySkipped) {
    ctx.cache_hit = false;
    // Build the actual key the stage will use
    char actual_key[cache::kCacheKeyMaxLen];
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    ki.selected_field_count = ctx.selected_field_count;
    ki.selected_fields = ctx.selected_fields;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    cache::build_cache_key(ki, actual_key, sizeof(actual_key));

    // Fill shards using the same key
    runtime::L2LookupJob job{};
    std::strcpy(job.key, actual_key);
    for (int i = 0; i < 100; ++i) {
        runtime::worker_queue_try_enqueue_lookup(worker_queue.get(), job);
    }

    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_lookup_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "enqueue-failed");

    std::size_t total_count = 0;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        total_count += worker_queue->shards[i].lookup_count;
    }
    EXPECT_EQ(total_count, 1u);
}

TEST_F(L2CacheAsyncLookupEnqueueStageTest, QueueFullContinuesPendingCleared) {
    ctx.cache_hit = false;
    // Build the actual key the stage will use
    char actual_key[cache::kCacheKeyMaxLen];
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    cache::build_cache_key(ki, actual_key, sizeof(actual_key));

    // Find shard for actual_key
    std::uint32_t shard_idx = static_cast<std::uint32_t>(
        bytetaper::hash::hash_cstr_runtime(actual_key) % runtime::kRuntimeShardCount);

    // Directly saturate lookup queue for that shard
    worker_queue->shards[shard_idx].lookup_count = runtime::kRuntimeQueueSlotsPerShard;

    cache_key_prepare_stage(ctx);
    auto output = l2_cache_async_lookup_enqueue_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "enqueue-failed");

    std::size_t pending_count = 0;
    for (std::size_t i = 0; i < runtime::kRuntimeShardCount; ++i) {
        pending_count += worker_queue->shards[i].pending_count;
    }
    EXPECT_EQ(pending_count, 0u); // Must be cleared on failed enqueue
}

} // namespace bytetaper::stages

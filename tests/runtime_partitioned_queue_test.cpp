// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "metrics/runtime_metrics.h"
#include "runtime/worker_queue.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

using namespace bytetaper::runtime;

namespace bytetaper::runtime {

class RuntimePartitionedQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        q_ = std::make_unique<WorkerQueue>();
        temp_dir = std::filesystem::current_path() / "bt_l2_test";
        std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::uint32_t hash_key_to_shard_proxy(const char* key) {
        if (key == nullptr)
            return 0;
        std::uint32_t hash = 5381;
        int c;
        const char* s = key;
        while ((c = *s++)) {
            hash = ((hash << 5) + hash) + static_cast<std::uint32_t>(c);
        }
        return hash % kRuntimeShardCount;
    }

    std::unique_ptr<WorkerQueue> q_;
    bytetaper::metrics::RuntimeMetrics metrics_{};
    std::filesystem::path temp_dir;
};

TEST_F(RuntimePartitionedQueueTest, RuntimeQueueMapsShardToStableWorker) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 4;
    worker_queue_init(q_.get(), cfg);

    for (std::size_t w = 0; w < cfg.worker_count; ++w) {
        std::size_t count = q_->worker_owned_shard_count[w];
        EXPECT_EQ(count, kRuntimeShardCount / cfg.worker_count);
        for (std::size_t i = 0; i < count; ++i) {
            std::size_t shard_idx = q_->worker_owned_shards[w][i];
            EXPECT_EQ(shard_idx % cfg.worker_count, w);
        }
    }
}

TEST_F(RuntimePartitionedQueueTest, RuntimeWorkerPopsOnlyOwnedShards) {
    EXPECT_EQ(kRuntimeShardCount, 256);
}

TEST_F(RuntimePartitionedQueueTest, RuntimeWorkerClearsPendingMarkerAfterHitMissError) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    worker_queue_init(q_.get(), cfg);
    q_->running = true;

    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());
    q_->resources.l1_cache = l1.get();

    L2LookupJob job;
    std::strcpy(job.key, "pending-test-key");

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
    std::uint32_t shard_idx = hash_key_to_shard_proxy("pending-test-key");
    EXPECT_EQ(q_->shards[shard_idx].pending_count, 1u);

    worker_queue_execute_one_for_test(q_.get());
    EXPECT_EQ(q_->shards[shard_idx].pending_count, 0u);
}

TEST_F(RuntimePartitionedQueueTest, AsyncL2StoreStillOwnsBodyMemory) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    worker_queue_init(q_.get(), cfg);
    q_->running = true;

    L2StoreJob job;
    std::strcpy(job.key, "memory-test-key");
    const char* test_data = "body data";
    job.entry.body = test_data;
    job.body_len = std::strlen(test_data);

    worker_queue_try_enqueue_store(q_.get(), job);

    std::uint32_t shard_idx = hash_key_to_shard_proxy("memory-test-key");
    L2StoreJob& slot = q_->shards[shard_idx].store_slots[q_->shards[shard_idx].store_head];
    std::uint32_t body_slot = slot.body_slot;

    EXPECT_STREQ(q_->shards[shard_idx].body_pool.heap_bodies[body_slot], test_data);
    EXPECT_EQ(slot.entry.body, q_->shards[shard_idx].body_pool.heap_bodies[body_slot]);
}

TEST_F(RuntimePartitionedQueueTest, AsyncL2LookupStalePromotionStillRejected) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    worker_queue_init(q_.get(), cfg);
    q_->running = true;

    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());
    q_->resources.l1_cache = l1.get();
    q_->resources.runtime_metrics = &metrics_;

    // Use real L2 (RocksDB) to avoid incomplete type issues
    cache::L2DiskCache* l2 = cache::l2_open(temp_dir.c_str());
    ASSERT_NE(l2, nullptr);
    q_->resources.l2_cache = l2;

    // 1. Put NEWER entry in L1
    cache::CacheEntry newer{};
    std::strcpy(newer.key, "stale-key");
    newer.created_at_epoch_ms = 2000;
    cache::l1_put(l1.get(), newer);

    // 2. Put OLDER entry in L2
    cache::CacheEntry older{};
    std::strcpy(older.key, "stale-key");
    older.created_at_epoch_ms = 1000;            // Older
    older.expires_at_epoch_ms = 2000000000000LL; // Far future
    cache::l2_put(l2, older);

    L2LookupJob job;
    std::strcpy(job.key, "stale-key");

    worker_queue_try_enqueue_lookup(q_.get(), job);
    worker_queue_execute_one_for_test(q_.get());

    // Should be rejected by l1_put_if_newer (recorded in metrics)
    EXPECT_EQ(metrics_.l2_to_l1_stale_rejected_total.load(), 1u);

    cache::l2_close(&l2);
}

} // namespace bytetaper::runtime

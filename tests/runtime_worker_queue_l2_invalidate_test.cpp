// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l2_disk_cache.h"
#include "hash/hash.h"
#include "runtime/worker_queue.h"

#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>

using namespace bytetaper::runtime;
using namespace bytetaper::cache;

class RuntimeWorkerQueueL2InvalidateTest : public ::testing::Test {
protected:
    std::string db_path = "test_wq_invalidate_db";
    L2DiskCache* l2 = nullptr;
    WorkerQueue queue{};

    void SetUp() override {
        bytetaper::hash::set_process_hash_seed_for_test({ 0x1234, 0x5678 });
        std::filesystem::remove_all(db_path);
        l2 = l2_open(db_path.c_str());
        ASSERT_NE(l2, nullptr);

        WorkerQueueConfig wq_config;
        wq_config.worker_count = 1;
        wq_config.invalidate_lane_quota = 1;
        ASSERT_EQ(worker_queue_init(&queue, wq_config), nullptr);

        // We do NOT call worker_queue_start because we want to execute jobs manually
        queue.resources.l2_cache = l2;
        queue.running.store(true, std::memory_order_release);
    }

    void TearDown() override {
        worker_queue_shutdown(&queue);
        if (l2) {
            l2_close(&l2);
        }
        std::filesystem::remove_all(db_path);
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    std::size_t get_shard(const char* key) {
        return bytetaper::hash::hash_cstr_runtime(key) % kRuntimeShardCount;
    }

    std::string key_for_shard(std::size_t target_shard) {
        for (int i = 0; i < 10000; ++i) {
            std::string key = "key_" + std::to_string(i);
            if (get_shard(key.c_str()) == target_shard) {
                return key;
            }
        }
        return "";
    }
};

TEST_F(RuntimeWorkerQueueL2InvalidateTest, EnqueueInvalidationDeletesExistingL2Key) {
    CacheEntry entry;
    std::strncpy(entry.key, "k1", sizeof(entry.key) - 1);
    entry.body = "v1";
    entry.body_len = 2;
    entry.expires_at_epoch_ms = 11000;
    entry.created_at_epoch_ms = 1000;
    char enc_buf[2048];
    l2_put_result(l2, entry, enc_buf, sizeof(enc_buf));

    CacheEntry hit;
    char scratch[2048];
    ASSERT_EQ(l2_get_result(l2, "k1", 1500, &hit, scratch, sizeof(scratch)), L2GetResult::Hit);

    EXPECT_TRUE(worker_queue_enqueue_l2_invalidate(&queue, "k1", 1500));
    EXPECT_TRUE(worker_queue_execute_one_for_test(&queue));

    EXPECT_EQ(l2_get_result(l2, "k1", 1500, &hit, scratch, sizeof(scratch)), L2GetResult::Miss);
}

TEST_F(RuntimeWorkerQueueL2InvalidateTest, EnqueueInvalidationMissingKeyDoesNotCrash) {
    EXPECT_TRUE(worker_queue_enqueue_l2_invalidate(&queue, "missing", 1500));
    EXPECT_TRUE(worker_queue_execute_one_for_test(&queue));
}

TEST_F(RuntimeWorkerQueueL2InvalidateTest, EnqueueInvalidationFailsWhenQueueNull) {
    EXPECT_FALSE(worker_queue_enqueue_l2_invalidate(nullptr, "key", 0));
}

TEST_F(RuntimeWorkerQueueL2InvalidateTest, EnqueueInvalidationFailsWhenKeyNull) {
    EXPECT_FALSE(worker_queue_enqueue_l2_invalidate(&queue, nullptr, 0));
    EXPECT_FALSE(worker_queue_enqueue_l2_invalidate(&queue, "", 0));
}

TEST_F(RuntimeWorkerQueueL2InvalidateTest, InvalidationLaneIsBounded) {
    // Fill one shard
    // queue.running is already true from SetUp, and no workers are running.

    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(worker_queue_enqueue_l2_invalidate(&queue, "k", 0));
    }
    // 17th should fail for that specific shard
    EXPECT_FALSE(worker_queue_enqueue_l2_invalidate(&queue, "k", 0));
}

TEST_F(RuntimeWorkerQueueL2InvalidateTest, InvalidationDoesNotBlockLookupLane) {
    std::string key = key_for_shard(42);
    ASSERT_EQ(get_shard(key.c_str()), 42);

    // Seed L2
    CacheEntry entry;
    std::strncpy(entry.key, key.c_str(), sizeof(entry.key) - 1);
    entry.body = "v1";
    entry.body_len = 2;
    entry.expires_at_epoch_ms = 11000;
    entry.created_at_epoch_ms = 1000;
    char enc_buf[2048];
    l2_put_result(l2, entry, enc_buf, sizeof(enc_buf));

    // Enqueue lookup then invalidate for same shard
    L2LookupJob lookup_job{};
    std::strncpy(lookup_job.key, key.c_str(), sizeof(lookup_job.key) - 1);
    lookup_job.key_hash =
        static_cast<std::uint32_t>(bytetaper::hash::hash_cstr_runtime(key.c_str()));
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(&queue, lookup_job));
    EXPECT_TRUE(worker_queue_enqueue_l2_invalidate(&queue, key.c_str(), 1500));

    RuntimeShard& shard = queue.shards[42];
    EXPECT_EQ(shard.lookup_count, 1);
    EXPECT_EQ(shard.invalidate_count, 1);

    // First execution should drain lookup only
    EXPECT_TRUE(worker_queue_execute_one_for_test(&queue));

    EXPECT_EQ(shard.lookup_count, 0);
    EXPECT_EQ(shard.invalidate_count, 1);

    // Key should still be in L2
    CacheEntry hit;
    char scratch[2048];
    EXPECT_EQ(l2_get_result(l2, key.c_str(), 1500, &hit, scratch, sizeof(scratch)),
              L2GetResult::Hit);

    // Second execution should drain invalidate
    EXPECT_TRUE(worker_queue_execute_one_for_test(&queue));
    EXPECT_EQ(shard.invalidate_count, 0);
    EXPECT_EQ(l2_get_result(l2, key.c_str(), 1500, &hit, scratch, sizeof(scratch)),
              L2GetResult::Miss);
}

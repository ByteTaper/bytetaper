// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"
#include "hash/hash.h"
#include "metrics/runtime_metrics.h"
#include "runtime/worker_queue.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

using namespace bytetaper::runtime;

class WorkerQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
        q_ = std::make_unique<WorkerQueue>();
    }

    void TearDown() override {
        worker_queue_shutdown(q_.get());
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    std::uint32_t expected_shard(const std::string& key) const {
        return static_cast<std::uint32_t>(bytetaper::hash::hash_cstr_runtime(key.c_str()) %
                                          kRuntimeShardCount);
    }

    std::uint32_t expected_shard(const char* key) const {
        return static_cast<std::uint32_t>(bytetaper::hash::hash_cstr_runtime(key) %
                                          kRuntimeShardCount);
    }

    std::string key_for_shard(const char* prefix, std::size_t shard_idx, int ordinal) const {
        int seen = 0;
        for (int i = 0; i < 100000; ++i) {
            std::string candidate = std::string(prefix) + std::to_string(i);
            if (expected_shard(candidate) != shard_idx) {
                continue;
            }
            if (seen == ordinal) {
                return candidate;
            }
            seen++;
        }
        return {};
    }

    std::unique_ptr<WorkerQueue> q_;
    bytetaper::metrics::RuntimeMetrics metrics_{};
};

TEST_F(WorkerQueueTest, InitAndStartStop) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 2;

    const char* err = worker_queue_init(q_.get(), cfg);
    ASSERT_EQ(err, nullptr) << "Init failed: " << (err ? err : "");
    WorkerQueueResources res{};
    res.runtime_metrics = &metrics_;
    EXPECT_EQ(worker_queue_start(q_.get(), res), nullptr);

    worker_queue_shutdown(q_.get());
}

TEST_F(WorkerQueueTest, EnqueueSucceeds) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    WorkerQueueResources res{};
    res.runtime_metrics = &metrics_;
    EXPECT_EQ(worker_queue_start(q_.get(), res), nullptr);

    L2StoreJob job;
    std::strcpy(job.key, "test-key");
    job.entry.body = "hello";
    job.body_len = 5;

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job));

    worker_queue_shutdown(q_.get());
}

TEST_F(WorkerQueueTest, QueueFullReturnsFalse) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    const char* err = worker_queue_init(q_.get(), cfg);
    ASSERT_EQ(err, nullptr) << "Init failed: " << (err ? err : "");

    {
        q_->running = true;
        q_->resources.runtime_metrics = &metrics_;
    }

    L2StoreJob job;
    std::strcpy(job.key, "test-key");
    job.entry.body = "data";
    job.body_len = 4;

    const std::uint32_t shard_idx = expected_shard("test-key");
    q_->shards[shard_idx].store_count = kRuntimeQueueSlotsPerShard;

    EXPECT_FALSE(worker_queue_try_enqueue_store(q_.get(), job));
    EXPECT_EQ(metrics_.worker_enqueue_dropped_total.load(), 1u);
}

TEST_F(WorkerQueueTest, EnqueueAfterShutdownReturnsFalse) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    WorkerQueueResources res{};
    res.runtime_metrics = &metrics_;
    EXPECT_EQ(worker_queue_start(q_.get(), res), nullptr);
    worker_queue_shutdown(q_.get());

    L2StoreJob job;
    EXPECT_FALSE(worker_queue_try_enqueue_store(q_.get(), job));
}

TEST_F(WorkerQueueTest, BodyPointerFixedInSlot) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    {
        q_->running = true;
    }

    L2StoreJob job;
    const char* original_body = "hello world";
    job.entry.body = original_body;
    job.body_len = std::strlen(original_body);
    std::strcpy(job.key, "key1");

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job));

    // Find which shard it went to
    std::size_t found_shard = 9999;
    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        if (q_->shards[i].store_count > 0) {
            found_shard = i;
            break;
        }
    }
    ASSERT_NE(found_shard, 9999u);

    // Check that slot entry body points inside shard body pool
    std::uint32_t slot_idx = q_->shards[found_shard].store_slots[0].body_slot;
    EXPECT_EQ(q_->shards[found_shard].store_slots[0].entry.body,
              q_->shards[found_shard].body_pool.bodies[slot_idx]);
    EXPECT_NE(q_->shards[found_shard].store_slots[0].entry.body, original_body);
    EXPECT_STREQ(q_->shards[found_shard].body_pool.bodies[slot_idx], original_body);
}

TEST_F(WorkerQueueTest, ConfiguredAsyncStoreMaxBodySizeControlsAdmission) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.async_store_max_body_size = 96 * 1024;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;
    q_->resources.runtime_metrics = &metrics_;

    std::string allowed_body(cfg.async_store_max_body_size, 'a');
    L2StoreJob allowed_job;
    std::strcpy(allowed_job.key, "configured-cap-allowed");
    allowed_job.entry.body = allowed_body.c_str();
    allowed_job.body_len = allowed_body.size();
    ASSERT_TRUE(worker_queue_try_enqueue_store(q_.get(), allowed_job));

    const std::uint32_t shard_idx = expected_shard("configured-cap-allowed");
    ASSERT_EQ(q_->shards[shard_idx].store_count, 1u);
    const std::uint32_t body_slot = q_->shards[shard_idx].store_slots[0].body_slot;
    EXPECT_EQ(q_->shards[shard_idx].body_pool.body_sizes[body_slot], cfg.async_store_max_body_size);
    EXPECT_EQ(q_->shards[shard_idx].body_pool.bodies[body_slot][0], 'a');

    std::string oversized_body(cfg.async_store_max_body_size + 1, 'b');
    L2StoreJob oversized_job;
    std::strcpy(oversized_job.key, "configured-cap-oversized");
    oversized_job.entry.body = oversized_body.c_str();
    oversized_job.body_len = oversized_body.size();
    EXPECT_FALSE(worker_queue_try_enqueue_store(q_.get(), oversized_job));
    EXPECT_EQ(metrics_.l2_async_store_oversized_skipped_total.load(), 1u);
}

TEST_F(WorkerQueueTest, InitInvalidWorkerCountZero) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 0;
    EXPECT_STREQ(worker_queue_init(q_.get(), cfg), "invalid worker_count");
}

TEST_F(WorkerQueueTest, InitWorkerCountExceedsMax) {
    WorkerQueueConfig cfg;
    cfg.worker_count = kWorkerQueueMaxWorkers + 1;
    EXPECT_STREQ(worker_queue_init(q_.get(), cfg), "invalid worker_count");
}

TEST_F(WorkerQueueTest, RuntimeQueueMapsKeyToStableShard) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    worker_queue_init(q_.get(), cfg);
    q_->running = true;

    L2StoreJob job;
    std::strcpy(job.key, "stable-key");

    std::uint32_t first_shard = 9999;
    for (int i = 0; i < 3; i++) {
        worker_queue_try_enqueue_store(q_.get(), job);
        std::uint32_t current_shard = 9999;
        for (std::uint32_t s = 0; s < kRuntimeShardCount; s++) {
            if (q_->shards[s].store_count > 0) {
                current_shard = s;
                q_->shards[s].store_count = 0; // reset
                q_->shards[s].store_head = 0;
                q_->shards[s].store_tail = 0;
                q_->shards[s].body_pool.occupied[0] = false;
                break;
            }
        }
        if (i == 0)
            first_shard = current_shard;
        else
            EXPECT_EQ(first_shard, current_shard);
    }
}

TEST_F(WorkerQueueTest, RuntimeQueueSameKeyDedupesPendingLookup) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    worker_queue_init(q_.get(), cfg);
    q_->running = true;

    L2LookupJob job;
    std::strcpy(job.key, "dedupe-key");

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
    // Second enqueue for same key (while first is still in queue) should fail
    EXPECT_FALSE(worker_queue_try_enqueue_lookup(q_.get(), job));
}

TEST_F(WorkerQueueTest, RuntimeQueueShardFullDoesNotAffectOtherShard) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    worker_queue_init(q_.get(), cfg);
    q_->running = true;

    L2StoreJob jobA;
    std::strcpy(jobA.key, "key-shard-a");
    L2StoreJob jobB;
    std::strcpy(jobB.key, "key-shard-b");

    // Verify they are different
    std::uint32_t shardA = 0, shardB = 0;
    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), jobA));
    for (std::uint32_t i = 0; i < kRuntimeShardCount; i++) {
        if (q_->shards[i].store_count > 0) {
            shardA = i;
        }
    }
    q_->shards[shardA].store_count = 0;
    q_->shards[shardA].store_tail = 0;
    q_->shards[shardA].store_head = 0;
    q_->shards[shardA].body_pool.occupied[0] = false;

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), jobB));
    for (std::uint32_t i = 0; i < kRuntimeShardCount; i++) {
        if (q_->shards[i].store_count > 0) {
            shardB = i;
        }
    }
    q_->shards[shardB].store_count = 0;
    q_->shards[shardB].store_tail = 0;
    q_->shards[shardB].store_head = 0;
    q_->shards[shardB].body_pool.occupied[0] = false;

    if (shardA == shardB) {
        std::strcpy(jobB.key, "another-key");
        worker_queue_try_enqueue_store(q_.get(), jobB);
        for (std::uint32_t i = 0; i < kRuntimeShardCount; i++) {
            if (q_->shards[i].store_count > 0) {
                shardB = i;
            }
        }
        q_->shards[shardB].store_count = 0;
        q_->shards[shardB].body_pool.occupied[0] = false;
    }
    ASSERT_NE(shardA, shardB);

    // Fill shard A
    for (std::size_t i = 0; i < kRuntimeQueueSlotsPerShard; i++) {
        EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), jobA));
    }
    EXPECT_FALSE(worker_queue_try_enqueue_store(q_.get(), jobA)); // Shard A full

    // Shard B should still accept
    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), jobB));
}

TEST_F(WorkerQueueTest, LookupJobDoesNotTouchBodyPool) {
    // Assert structural memory budget constraints
    EXPECT_LT(sizeof(L2LookupJob), 1500u);
    EXPECT_LT(sizeof(L2StoreJob), 3000u);

    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2LookupJob job;
    std::strcpy(job.key, "lookup-test-key");

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));

    // Find shard
    std::size_t shard_idx = 9999;
    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        if (q_->shards[i].lookup_count > 0) {
            shard_idx = i;
            break;
        }
    }
    ASSERT_NE(shard_idx, 9999u);

    // Assert that the body pool in this shard is entirely unoccupied
    for (std::size_t i = 0; i < kRuntimeQueueSlotsPerShard; ++i) {
        EXPECT_FALSE(q_->shards[shard_idx].body_pool.occupied[i]);
    }
}

TEST_F(WorkerQueueTest, MixedLookupAndStoreBothEnqueue) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2LookupJob lookup_job;
    std::strcpy(lookup_job.key, "mixed-key");

    L2StoreJob store_job;
    std::strcpy(store_job.key, "mixed-key");
    store_job.entry.body = "data";
    store_job.body_len = 4;

    // Both can be enqueued to the same shard's independent rings
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), lookup_job));
    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), store_job));

    // Find shard
    std::size_t shard_idx = 9999;
    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        if (q_->shards[i].lookup_count > 0) {
            shard_idx = i;
            break;
        }
    }
    ASSERT_NE(shard_idx, 9999u);

    EXPECT_EQ(q_->shards[shard_idx].lookup_count, 1u);
    EXPECT_EQ(q_->shards[shard_idx].store_count, 1u);
    EXPECT_TRUE(
        q_->shards[shard_idx].body_pool.occupied[q_->shards[shard_idx].store_slots[0].body_slot]);
}

TEST_F(WorkerQueueTest, PendingHashDuplicateKeyRejected) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2LookupJob job;
    std::strcpy(job.key, "dup-key");

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
    EXPECT_FALSE(worker_queue_try_enqueue_lookup(q_.get(), job)); // Same key should be rejected
}

TEST_F(WorkerQueueTest, PendingHashDifferentKeysEachAccepted) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2LookupJob job1;
    std::strcpy(job1.key, "key-1");

    L2LookupJob job2;
    std::strcpy(job2.key, "key-2");

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job1));
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job2)); // Different keys should succeed
}

TEST_F(WorkerQueueTest, PendingHashClearedAfterExecution) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    // Set mock L1 to allow successful execution
    auto l1 = std::make_unique<bytetaper::cache::L1Cache>();
    bytetaper::cache::l1_init(l1.get());
    q_->resources.l1_cache = l1.get();

    L2LookupJob job;
    std::strcpy(job.key, "execution-clear-key");

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
    worker_queue_execute_one_for_test(q_.get());

    // Should be able to enqueue again as it was successfully executed and cleared
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
}

TEST_F(WorkerQueueTest, WorkerQueueOwnershipInvariants) {
    // Case 1: worker_count = 1
    {
        WorkerQueueConfig cfg;
        cfg.worker_count = 1;
        ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

        EXPECT_EQ(q_->worker_owned_shard_count[0], kRuntimeShardCount);
        for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
            EXPECT_EQ(q_->worker_owned_shards[0][i], i);
        }
    }

    // Case 2: worker_count = 2
    {
        WorkerQueueConfig cfg;
        cfg.worker_count = 2;
        ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

        EXPECT_EQ(q_->worker_owned_shard_count[0], kRuntimeShardCount / 2);
        EXPECT_EQ(q_->worker_owned_shard_count[1], kRuntimeShardCount / 2);

        bool seen[kRuntimeShardCount] = { false };
        for (std::size_t w = 0; w < cfg.worker_count; ++w) {
            for (std::size_t i = 0; i < q_->worker_owned_shard_count[w]; ++i) {
                std::size_t shard_idx = q_->worker_owned_shards[w][i];
                ASSERT_LT(shard_idx, kRuntimeShardCount);
                EXPECT_FALSE(seen[shard_idx]);
                seen[shard_idx] = true;
                EXPECT_EQ(shard_idx % cfg.worker_count, w);
            }
        }

        for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
            EXPECT_TRUE(seen[i]);
        }
    }

    // Case 3: worker_count = kWorkerQueueMaxWorkers
    {
        WorkerQueueConfig cfg;
        cfg.worker_count = kWorkerQueueMaxWorkers;
        ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

        bool seen[kRuntimeShardCount] = { false };
        std::size_t total_count = 0;
        for (std::size_t w = 0; w < cfg.worker_count; ++w) {
            std::size_t count = q_->worker_owned_shard_count[w];
            total_count += count;
            EXPECT_EQ(count, kRuntimeShardCount / kWorkerQueueMaxWorkers);
            for (std::size_t i = 0; i < count; ++i) {
                std::size_t shard_idx = q_->worker_owned_shards[w][i];
                ASSERT_LT(shard_idx, kRuntimeShardCount);
                EXPECT_FALSE(seen[shard_idx]);
                seen[shard_idx] = true;
                EXPECT_EQ(shard_idx % cfg.worker_count, w);
            }
        }

        EXPECT_EQ(total_count, kRuntimeShardCount);
        for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
            EXPECT_TRUE(seen[i]);
        }
    }
}

TEST_F(WorkerQueueTest, DeterministicWakeupOnNonPrimaryShard) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    // Seed mock L1 to allow successful execution
    auto l1 = std::make_unique<bytetaper::cache::L1Cache>();
    bytetaper::cache::l1_init(l1.get());
    q_->resources.l1_cache = l1.get();

    // Start queue
    WorkerQueueResources res{};
    res.runtime_metrics = &metrics_;
    res.l1_cache = l1.get();
    ASSERT_EQ(worker_queue_start(q_.get(), res), nullptr);

    // Find key for non-primary shard (shard 1)
    std::string key;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "test-key-" + std::to_string(i);
        if (expected_shard(candidate) == 1) {
            key = candidate;
            break;
        }
    }
    ASSERT_FALSE(key.empty());

    L2LookupJob job;
    std::strcpy(job.key, key.c_str());

    // Initially shard 1 is empty and worker wake state is unsignaled
    EXPECT_EQ(q_->shards[1].lookup_count, 0u);

    // Enqueue
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));

    // Wait a brief moment for worker to execute without relying on 10ms timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    // Verify that the job was processed and shard 1 is empty
    {
        std::lock_guard<std::mutex> lock(q_->shards[1].mu);
        EXPECT_EQ(q_->shards[1].lookup_count, 0u);
    }

    worker_queue_shutdown(q_.get());
}

TEST_F(WorkerQueueTest, ReadyQueueInitialState) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 4;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    for (std::size_t w = 0; w < cfg.worker_count; ++w) {
        EXPECT_EQ(q_->worker_ready[w].head, 0u);
        EXPECT_EQ(q_->worker_ready[w].tail, 0u);
        EXPECT_EQ(q_->worker_ready[w].count, 0u);
    }

    for (std::size_t s = 0; s < kRuntimeShardCount; ++s) {
        EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), s), RuntimeShardState::Idle);
    }
}

TEST_F(WorkerQueueTest, SingleReadyQueuePushPerShard) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2StoreJob job1;
    std::strcpy(job1.key, "key-shard-a");
    job1.body_len = 0;

    // First enqueue
    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job1));

    // Find which shard it went to
    std::size_t shard_idx = 9999;
    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        if (q_->shards[i].store_count > 0) {
            shard_idx = i;
            break;
        }
    }
    ASSERT_NE(shard_idx, 9999u);

    // Expect that the ready queue for worker 0 has count == 1
    EXPECT_EQ(q_->worker_ready[0].count, 1u);
    EXPECT_EQ(q_->worker_ready[0].shard_ids[q_->worker_ready[0].head], shard_idx);
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);

    // Second enqueue for another key that maps to the same shard
    L2StoreJob job2;
    std::string key2;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "test-key-collision-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            key2 = candidate;
            break;
        }
    }
    ASSERT_FALSE(key2.empty());
    std::strcpy(job2.key, key2.c_str());
    job2.body_len = 0;

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job2));

    // The ready queue should STILL have count == 1 (no duplicate push!)
    EXPECT_EQ(q_->worker_ready[0].count, 1u);
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
}

TEST_F(WorkerQueueTest, PrecedenceAndTypedPop) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    // We will enqueue a store job and a lookup job to the same shard
    L2StoreJob store_job;
    std::strcpy(store_job.key, "common-shard-key");
    store_job.body_len = 0;

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), store_job));

    // Find the shard index
    std::size_t shard_idx = 9999;
    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        if (q_->shards[i].store_count > 0) {
            shard_idx = i;
            break;
        }
    }
    ASSERT_NE(shard_idx, 9999u);

    // Find a key for lookup that hashes to the exact same shard
    std::string lookup_key;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "lookup-collision-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            lookup_key = candidate;
            break;
        }
    }
    ASSERT_FALSE(lookup_key.empty());

    L2LookupJob lookup_job;
    std::strcpy(lookup_job.key, lookup_key.c_str());

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), lookup_job));

    // Now try pop.
    // Assert lookup pop returns the lookup job
    L2LookupJob popped_lookup;
    EXPECT_TRUE(worker_queue_shard_try_pop_lookup_for_test(q_.get(), shard_idx, &popped_lookup));
    EXPECT_STREQ(popped_lookup.key, lookup_key.c_str());

    // Assert subsequent lookup pop returns false
    EXPECT_FALSE(worker_queue_shard_try_pop_lookup_for_test(q_.get(), shard_idx, &popped_lookup));

    // Next pop on store lane should be the store job
    L2StoreJob popped_store;
    EXPECT_TRUE(worker_queue_shard_try_pop_store_for_test(q_.get(), shard_idx, &popped_store));
    EXPECT_STREQ(popped_store.key, "common-shard-key");

    // Third pop on store lane should return false (empty)
    EXPECT_FALSE(worker_queue_shard_try_pop_store_for_test(q_.get(), shard_idx, &popped_store));
}

TEST_F(WorkerQueueTest, RequeueAndClearSemantics) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2StoreJob job1;
    std::strcpy(job1.key, "common-key");
    job1.body_len = 0;

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job1));

    // Find shard
    std::size_t shard_idx = 9999;
    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        if (q_->shards[i].store_count > 0) {
            shard_idx = i;
            break;
        }
    }
    ASSERT_NE(shard_idx, 9999u);

    // Enqueue a second job to the same shard to make sure work remains after one pop
    std::string key2;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "collision-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            key2 = candidate;
            break;
        }
    }
    ASSERT_FALSE(key2.empty());
    L2StoreJob job2;
    std::strcpy(job2.key, key2.c_str());
    job2.body_len = 0;
    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job2));

    // Clear the ready queue of worker 0 so we can test requeue from a clean slate
    q_->worker_ready[0].head = 0;
    q_->worker_ready[0].tail = 0;
    q_->worker_ready[0].count = 0;

    // Pop the first job
    L2StoreJob popped_store;
    EXPECT_TRUE(worker_queue_shard_try_pop_store_for_test(q_.get(), shard_idx, &popped_store));

    // Requeue since 1 job remains
    worker_queue_shard_requeue_or_clear_for_test(q_.get(), shard_idx);
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
    EXPECT_EQ(q_->worker_ready[0].count, 1u);
    EXPECT_EQ(q_->worker_ready[0].shard_ids[q_->worker_ready[0].head], shard_idx);

    // Pop the second job (now empty)
    EXPECT_TRUE(worker_queue_shard_try_pop_store_for_test(q_.get(), shard_idx, &popped_store));

    // Clear ready queue count again to isolate the next check
    q_->worker_ready[0].head = 0;
    q_->worker_ready[0].tail = 0;
    q_->worker_ready[0].count = 0;

    // Run requeue_or_clear (should clear state to Idle and NOT requeue)
    worker_queue_shard_requeue_or_clear_for_test(q_.get(), shard_idx);
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Idle);
    EXPECT_EQ(q_->worker_ready[0].count, 0u);
}

TEST_F(WorkerQueueTest, MixedLaneSchedulingFollowsQuota) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    // Map all test keys to shard 0
    std::size_t shard_idx = 0;

    // Enqueue 10 lookup jobs to shard 0
    std::string lookup_base = "lookup-quota-";
    int enqueued_lookups = 0;
    for (int i = 0; i < 100000 && enqueued_lookups < 10; ++i) {
        std::string candidate = lookup_base + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            L2LookupJob lookup_job;
            std::strcpy(lookup_job.key, candidate.c_str());
            if (worker_queue_try_enqueue_lookup(q_.get(), lookup_job)) {
                enqueued_lookups++;
            }
        }
    }
    ASSERT_EQ(enqueued_lookups, 10);

    // Enqueue 3 store jobs to shard 0
    std::string store_base = "store-quota-";
    int enqueued_stores = 0;
    for (int i = 0; i < 100000 && enqueued_stores < 3; ++i) {
        std::string candidate = store_base + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            L2StoreJob store_job;
            std::strcpy(store_job.key, candidate.c_str());
            store_job.body_len = 0;
            if (worker_queue_try_enqueue_store(q_.get(), store_job)) {
                enqueued_stores++;
            }
        }
    }
    ASSERT_EQ(enqueued_stores, 3);

    // Run exactly 1 event (will process ready shard 0)
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    // Assert that shard 0's lookup_count decreased by exactly 4 (from 10 to 6)
    EXPECT_EQ(q_->shards[shard_idx].lookup_count, 6u);

    // Assert that shard 0's store_count decreased by exactly 1 (from 3 to 2)
    EXPECT_EQ(q_->shards[shard_idx].store_count, 2u);

    // Since work remains, ready queue of worker 0 should still contain shard 0
    EXPECT_EQ(q_->worker_ready[0].count, 1u);
    EXPECT_EQ(q_->worker_ready[0].shard_ids[q_->worker_ready[0].head], shard_idx);
}

TEST_F(WorkerQueueTest, StoreStarvationSustainedPressure) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    // Find a target shard
    std::size_t shard_idx = 0;

    // Enqueue 1 store job to shard_idx
    std::string store_key;
    for (int i = 0; i < 100000; ++i) {
        std::string candidate = "store-key-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            store_key = candidate;
            break;
        }
    }
    ASSERT_FALSE(store_key.empty());
    L2StoreJob store_job;
    std::strcpy(store_job.key, store_key.c_str());
    store_job.body_len = 0;
    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), store_job));

    // Enqueue 10 lookup jobs to the same shard_idx to simulate sustained lookup load
    std::string lookup_base = "lookup-sustained-";
    int enqueued_lookups = 0;
    for (int i = 0; i < 100000 && enqueued_lookups < 10; ++i) {
        std::string candidate = lookup_base + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            L2LookupJob lookup_job;
            std::strcpy(lookup_job.key, candidate.c_str());
            if (worker_queue_try_enqueue_lookup(q_.get(), lookup_job)) {
                enqueued_lookups++;
            }
        }
    }
    ASSERT_EQ(enqueued_lookups, 10);

    // Run exactly one event. It must execute up to 4 lookup jobs AND 1 store job.
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    // Assert that the store job was executed (store_count is now 0)
    EXPECT_EQ(q_->shards[shard_idx].store_count, 0u);
    // Assert that lookup_count decreased by exactly 4 (from 10 to 6)
    EXPECT_EQ(q_->shards[shard_idx].lookup_count, 6u);
}

TEST_F(WorkerQueueTest, ShutdownDrainsPendingJobs) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    // Seed mock L1 to allow successful execution
    auto l1 = std::make_unique<bytetaper::cache::L1Cache>();
    bytetaper::cache::l1_init(l1.get());
    q_->resources.l1_cache = l1.get();

    WorkerQueueResources res{};
    res.runtime_metrics = &metrics_;
    res.l1_cache = l1.get();
    ASSERT_EQ(worker_queue_start(q_.get(), res), nullptr);

    std::size_t target_shard = 0; // We'll map them to shard 0

    // Enqueue 5 lookup jobs to the same shard
    std::string lookup_base = "drain-lookup-key-";
    int enqueued_lookups = 0;
    for (int i = 0; i < 100000 && enqueued_lookups < 5; ++i) {
        std::string candidate = lookup_base + std::to_string(i);
        if (expected_shard(candidate) == target_shard) {
            L2LookupJob job;
            std::strcpy(job.key, candidate.c_str());
            if (worker_queue_try_enqueue_lookup(q_.get(), job)) {
                enqueued_lookups++;
            }
        }
    }
    ASSERT_EQ(enqueued_lookups, 5);

    // Enqueue 5 store jobs to the same shard
    std::string store_base = "drain-store-key-";
    int enqueued_stores = 0;
    for (int i = 0; i < 100000 && enqueued_stores < 5; ++i) {
        std::string candidate = store_base + std::to_string(i);
        if (expected_shard(candidate) == target_shard) {
            L2StoreJob job;
            std::strcpy(job.key, candidate.c_str());
            job.body_len = 0;
            if (worker_queue_try_enqueue_store(q_.get(), job)) {
                enqueued_stores++;
            }
        }
    }
    ASSERT_EQ(enqueued_stores, 5);

    // Shutdown immediately. This will trigger the worker shutdown draining path.
    worker_queue_shutdown(q_.get());

    // Verify that both lookup and store jobs were drained and processed from shard 0 during
    // shutdown
    EXPECT_EQ(q_->shards[target_shard].lookup_count, 0u);
    EXPECT_EQ(q_->shards[target_shard].store_count, 0u);
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), target_shard), RuntimeShardState::Idle);
}

TEST_F(WorkerQueueTest, StateMachineProcessesReadyShard) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2StoreJob job;
    std::strcpy(job.key, "key-sm-1");
    job.body_len = 0;

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job));
    EXPECT_EQ(q_->worker_ready[0].count, 1u);

    // Run one event
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    EXPECT_EQ(q_->worker_ready[0].count, 0u);
    // Find shard and check store_count is 0
    std::size_t shard_idx = expected_shard("key-sm-1");
    EXPECT_EQ(q_->shards[shard_idx].store_count, 0u);
}

TEST_F(WorkerQueueTest, StateMachineTransitionsToDrainingOnShutdown) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    // Seed lookups / stores on owned shards
    L2StoreJob job;
    std::strcpy(job.key, "key-sm-2");
    job.body_len = 0;

    // Artificially put jobs in shard but do NOT push into ready queue
    std::size_t shard_idx = expected_shard("key-sm-2");
    q_->shards[shard_idx].store_slots[0] = job;
    q_->shards[shard_idx].store_count = 1;

    // Transition state: set running to false to simulate shutdown
    q_->running = false;

    // Run one event. Since ready queue is empty and running is false, it should drain and process
    // the job
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    // Shard should be drained
    EXPECT_EQ(q_->shards[shard_idx].store_count, 0u);
}

TEST_F(WorkerQueueTest, DrainingProcessesAllJobsAndStops) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    L2StoreJob job1, job2;
    std::strcpy(job1.key, "key-sm-3a");
    std::strcpy(job2.key, "key-sm-3b");

    // Manually enqueue without running threads or triggering push
    std::size_t s1 = expected_shard("key-sm-3a");
    std::size_t s2 = expected_shard("key-sm-3b");

    q_->shards[s1].store_slots[0] = job1;
    q_->shards[s1].store_count = 1;

    q_->shards[s2].store_slots[0] = job2;
    q_->shards[s2].store_count = 1;

    // Initially work remains
    EXPECT_EQ(q_->shards[s1].store_count, 1u);
    EXPECT_EQ(q_->shards[s2].store_count, 1u);

    // Simulate Shutdown by running one event when not running
    q_->running = false;
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    // Both should be drained
    EXPECT_EQ(q_->shards[s1].store_count, 0u);
    EXPECT_EQ(q_->shards[s2].store_count, 0u);
}

TEST_F(WorkerQueueTest, WorkerDoesNotProcessUnownedShards) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    L2StoreJob job;
    std::strcpy(job.key, "key-sm-5");
    std::size_t shard_idx = expected_shard("key-sm-5");
    std::size_t owner = shard_idx % 2;
    std::size_t non_owner = 1 - owner;

    q_->shards[shard_idx].store_slots[0] = job;
    q_->shards[shard_idx].store_count = 1;

    // Non-owner trying to drain should do nothing
    EXPECT_FALSE(worker_drain_owned_once(q_.get(), non_owner));
    EXPECT_EQ(q_->shards[shard_idx].store_count, 1u);

    // Owner trying to drain should process it
    EXPECT_TRUE(worker_drain_owned_once(q_.get(), owner));
    EXPECT_EQ(q_->shards[shard_idx].store_count, 0u);
}

TEST_F(WorkerQueueTest, ShutdownSignalWakesAllSleepingWorkers) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 4;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    WorkerQueueResources res{};
    res.runtime_metrics = &metrics_;
    ASSERT_EQ(worker_queue_start(q_.get(), res), nullptr);

    // Workers are now sleeping on wait_cv.
    // Call shutdown. It should wake them all up via notify_all and join them quickly.
    auto start = std::chrono::steady_clock::now();
    worker_queue_shutdown(q_.get());
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // It should join very quickly (well under 200ms)
    EXPECT_LT(duration_ms, 200);
}

TEST_F(WorkerQueueTest, DrainOwnedOnceDoesNotBlockOnEmpty) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    // Ensure all shards are empty (they are by default)
    // Call drain owned once. It should return false instantly and not block.
    auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(worker_drain_owned_once(q_.get(), 0));
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(duration_ms, 50);
}

TEST_F(WorkerQueueTest, StoreBodyPoolFullAdmission) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    {
        q_->running = true;
        q_->resources.runtime_metrics = &metrics_;
    }

    L2StoreJob job;
    const std::uint32_t target_shard = expected_shard("key-pool-test");
    job.entry.body = "payload";
    job.body_len = 7;

    for (std::size_t i = 0; i < kRuntimeQueueSlotsPerShard; ++i) {
        const std::string coll_key = key_for_shard("key-pool-", target_shard, static_cast<int>(i));
        ASSERT_FALSE(coll_key.empty());
        std::strcpy(job.key, coll_key.c_str());
        ASSERT_TRUE(worker_queue_try_enqueue_store(q_.get(), job)) << "Failed at index " << i;
    }

    const std::string overflow_key = key_for_shard("overflow-", target_shard, 0);
    ASSERT_FALSE(overflow_key.empty());
    std::strcpy(job.key, overflow_key.c_str());
    EXPECT_FALSE(worker_queue_try_enqueue_store(q_.get(), job));
    EXPECT_EQ(metrics_.worker_store_body_pool_full_total.load(), 1u);

    EXPECT_TRUE(worker_queue_execute_one_for_test(q_.get()));
    EXPECT_EQ(q_->shards[target_shard].store_count, kRuntimeQueueSlotsPerShard - 1);

    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job));
}

TEST_F(WorkerQueueTest, WorkerLaneWaitTelemetry) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    {
        q_->running = true;
        q_->resources.runtime_metrics = &metrics_;
    }

    L2LookupJob lookup_job{};
    std::strcpy(lookup_job.key, "lookup-wait-test");
    lookup_job.enqueued_at_ms = 100;

    ASSERT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), lookup_job));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(worker_queue_execute_one_for_test(q_.get()));

    EXPECT_EQ(metrics_.worker_lookup_lane_wait_count_total.load(), 1u);
    EXPECT_GT(metrics_.worker_lookup_lane_wait_ms_total.load(), 0u);

    L2StoreJob store_job{};
    std::strcpy(store_job.key, "store-wait-test");
    store_job.enqueued_at_ms = 100;
    ASSERT_TRUE(worker_queue_try_enqueue_store(q_.get(), store_job));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(worker_queue_execute_one_for_test(q_.get()));

    EXPECT_EQ(metrics_.worker_store_lane_wait_count_total.load(), 1u);
    EXPECT_GT(metrics_.worker_store_lane_wait_ms_total.load(), 0u);
}

TEST_F(WorkerQueueTest, StoreLaneStarvationDetection) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    {
        q_->running = true;
        q_->resources.runtime_metrics = &metrics_;
    }

    // Map keys to shard 0
    std::size_t target_shard = 0;

    for (int i = 0; i < 10; ++i) {
        const std::string candidate = key_for_shard("lookup-starve-", target_shard, i);
        ASSERT_FALSE(candidate.empty());
        L2LookupJob lookup_job;
        std::strcpy(lookup_job.key, candidate.c_str());
        ASSERT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), lookup_job));
    }

    for (int i = 0; i < 3; ++i) {
        const std::string store_key = key_for_shard("store-starve-", target_shard, i);
        ASSERT_FALSE(store_key.empty());
        L2StoreJob store_job;
        std::strcpy(store_job.key, store_key.c_str());
        store_job.body_len = 0;
        ASSERT_TRUE(worker_queue_try_enqueue_store(q_.get(), store_job));
    }

    EXPECT_EQ(metrics_.worker_store_lane_starvation_total.load(), 0u);

    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    EXPECT_EQ(q_->shards[target_shard].lookup_count, 6u);
    EXPECT_EQ(q_->shards[target_shard].store_count, 2u);
    EXPECT_EQ(metrics_.worker_store_lane_starvation_total.load(), 1u);
}

TEST_F(WorkerQueueTest, LookupLaneQuotaZeroFails) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.lookup_lane_quota = 0;
    const char* err = worker_queue_init(q_.get(), cfg);
    ASSERT_NE(err, nullptr);
    EXPECT_STREQ(err, "worker_queue: lookup_lane_quota must be >= 1");
}

TEST_F(WorkerQueueTest, StoreLaneQuotaZeroFails) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.store_lane_quota = 0;
    const char* err = worker_queue_init(q_.get(), cfg);
    ASSERT_NE(err, nullptr);
    EXPECT_STREQ(err, "worker_queue: store_lane_quota must be >= 1");
}

TEST_F(WorkerQueueTest, AsyncStoreMaxBodySizeAboveCapFails) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.async_store_max_body_size = kAsyncL2StoreAbsoluteMaxBodySize + 1;
    const char* err = worker_queue_init(q_.get(), cfg);
    ASSERT_NE(err, nullptr);
    EXPECT_STREQ(err, "worker_queue: async_store_max_body_size exceeds absolute cap");
}

TEST_F(WorkerQueueTest, DynamicLaneQuotasRespected) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.lookup_lane_quota = 6;
    cfg.store_lane_quota = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    std::size_t shard_idx = 0;

    // Enqueue 10 lookup jobs
    for (int i = 0; i < 10; ++i) {
        const std::string cand = key_for_shard("lookup-dyn-", shard_idx, i);
        L2LookupJob job;
        std::strcpy(job.key, cand.c_str());
        ASSERT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
    }

    // Enqueue 4 store jobs
    for (int i = 0; i < 4; ++i) {
        const std::string cand = key_for_shard("store-dyn-", shard_idx, i);
        L2StoreJob job;
        std::strcpy(job.key, cand.c_str());
        job.body_len = 0;
        ASSERT_TRUE(worker_queue_try_enqueue_store(q_.get(), job));
    }

    // Run one event
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    // Lookup should be decreased by exactly 6 (from 10 to 4)
    EXPECT_EQ(q_->shards[shard_idx].lookup_count, 4u);
    // Store should be decreased by exactly 2 (from 4 to 2)
    EXPECT_EQ(q_->shards[shard_idx].store_count, 2u);
}

TEST_F(WorkerQueueTest, PrometheusEffectiveMetricsRendered) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 3;
    cfg.lookup_lane_quota = 5;
    cfg.store_lane_quota = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);

    WorkerQueueResources res{};
    res.runtime_metrics = &metrics_;
    ASSERT_EQ(worker_queue_start(q_.get(), res), nullptr);

    char buf[16384];
    std::size_t rendered =
        bytetaper::metrics::render_runtime_metrics_prometheus(metrics_, buf, sizeof(buf));
    ASSERT_GT(rendered, 0u);
    std::string output(buf, rendered);

    EXPECT_NE(
        output.find(
            "# TYPE bytetaper_worker_count_effective gauge\nbytetaper_worker_count_effective 3\n"),
        std::string::npos);
    EXPECT_NE(output.find("# TYPE bytetaper_worker_lookup_lane_quota_effective "
                          "gauge\nbytetaper_worker_lookup_lane_quota_effective 5\n"),
              std::string::npos);
    EXPECT_NE(output.find("# TYPE bytetaper_worker_store_lane_quota_effective "
                          "gauge\nbytetaper_worker_store_lane_quota_effective 2\n"),
              std::string::npos);

    worker_queue_shutdown(q_.get());
}

TEST_F(WorkerQueueTest, StarvationCorrectlyTriggeredWithCustomLaneQuota) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.lookup_lane_quota = 3;
    cfg.store_lane_quota = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;
    q_->resources.runtime_metrics = &metrics_;

    std::size_t target_shard = 0;

    // Enqueue 3 lookups (exactly matching the custom lookup quota)
    for (int i = 0; i < 3; ++i) {
        const std::string candidate = key_for_shard("lookup-starve-custom-", target_shard, i);
        L2LookupJob lookup_job;
        std::strcpy(lookup_job.key, candidate.c_str());
        ASSERT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), lookup_job));
    }

    // Enqueue 3 stores (exceeding custom store quota of 2)
    for (int i = 0; i < 3; ++i) {
        const std::string store_key = key_for_shard("store-starve-custom-", target_shard, i);
        L2StoreJob store_job;
        std::strcpy(store_job.key, store_key.c_str());
        store_job.body_len = 0;
        ASSERT_TRUE(worker_queue_try_enqueue_store(q_.get(), store_job));
    }

    EXPECT_EQ(metrics_.worker_store_lane_starvation_total.load(), 0u);

    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));

    // After 1 cycle:
    // lookups drained: 3 (count becomes 0)
    // stores drained: 2 (count becomes 1)
    EXPECT_EQ(q_->shards[target_shard].lookup_count, 0u);
    EXPECT_EQ(q_->shards[target_shard].store_count, 1u);

    // Starvation triggers because:
    // lookup_executed == lookup_lane_quota (3)
    // AND store_executed == store_lane_quota (2)
    // AND stores still remain (1)
    EXPECT_EQ(metrics_.worker_store_lane_starvation_total.load(), 1u);
}

TEST_F(WorkerQueueTest, DynamicAsyncStoreMaxBodySizeValidation) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.async_store_max_body_size = 4096;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;
    q_->resources.runtime_metrics = &metrics_;

    // Allowed body
    std::string allowed_body(4096, 'x');
    L2StoreJob allowed_job;
    std::strcpy(allowed_job.key, "key-allowed-custom");
    allowed_job.entry.body = allowed_body.c_str();
    allowed_job.body_len = allowed_body.size();
    EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), allowed_job));

    // Oversized body
    std::string oversized_body(4097, 'y');
    L2StoreJob oversized_job;
    std::strcpy(oversized_job.key, "key-oversized-custom");
    oversized_job.entry.body = oversized_body.c_str();
    oversized_job.body_len = oversized_body.size();
    EXPECT_FALSE(worker_queue_try_enqueue_store(q_.get(), oversized_job));
    EXPECT_EQ(metrics_.l2_async_store_oversized_skipped_total.load(), 1u);
    EXPECT_FALSE(oversized_job.body_len == 0); // use oversized_job to avoid unused variable warning
}

TEST_F(WorkerQueueTest, ShardStateStartsIdle) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    for (std::size_t s = 0; s < kRuntimeShardCount; ++s) {
        EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), s), RuntimeShardState::Idle);
    }
}

TEST_F(WorkerQueueTest, EnqueueTransitionsIdleToQueued) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2LookupJob job;
    std::strcpy(job.key, "key-a");
    std::size_t shard_idx = expected_shard("key-a");
    std::size_t owner = shard_idx % cfg.worker_count;

    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Idle);
    EXPECT_EQ(q_->worker_ready[owner].count, 0u);

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));

    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
    EXPECT_EQ(q_->worker_ready[owner].count, 1u);
}

TEST_F(WorkerQueueTest, DuplicateEnqueueDoesNotDuplicateReadyEvent) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    std::size_t shard_idx = expected_shard("key-a");
    std::size_t owner = shard_idx % cfg.worker_count;

    L2LookupJob job1;
    std::strcpy(job1.key, "key-a");
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job1));
    EXPECT_EQ(q_->worker_ready[owner].count, 1u);

    // Collision key for same shard
    std::string key2;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "key-collision-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            key2 = candidate;
            break;
        }
    }
    ASSERT_FALSE(key2.empty());
    L2LookupJob job2;
    std::strcpy(job2.key, key2.c_str());

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job2));
    EXPECT_EQ(q_->worker_ready[owner].count, 1u);
}

TEST_F(WorkerQueueTest, WorkerPopTransitionsQueuedToProcessing) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    std::size_t shard_idx = 0;
    L2LookupJob job;
    std::string key0;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "key-find-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            key0 = candidate;
            break;
        }
    }
    ASSERT_FALSE(key0.empty());
    std::strcpy(job.key, key0.c_str());

    // Enqueue -> state becomes Queued, pushed to ready queue
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);

    // 1. Verify Queued -> Processing transition succeeds
    EXPECT_TRUE(worker_queue_shard_try_mark_processing_for_test(q_.get(), 0, shard_idx));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx),
              RuntimeShardState::Processing);

    // 2. Verify subsequent transition attempt fails (already Processing)
    EXPECT_FALSE(worker_queue_shard_try_mark_processing_for_test(q_.get(), 0, shard_idx));

    // 3. Verify transition attempt on Idle shard fails
    worker_queue_shard_set_state_for_test(q_.get(), shard_idx, RuntimeShardState::Idle);
    EXPECT_FALSE(worker_queue_shard_try_mark_processing_for_test(q_.get(), 0, shard_idx));
}

TEST_F(WorkerQueueTest, WorkerPopRejectsInvalidState) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    std::size_t shard_idx = 0;
    L2LookupJob job;
    std::string key0;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "key-find-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            key0 = candidate;
            break;
        }
    }
    ASSERT_FALSE(key0.empty());
    std::strcpy(job.key, key0.c_str());

    // Enqueue -> state becomes Queued, pushed to ready queue
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);

    // Manually set state back to Idle
    worker_queue_shard_set_state_for_test(q_.get(), shard_idx, RuntimeShardState::Idle);

    // Run one event -> pops the ready event, but rejects processing because state is not Queued!
    // So the job remains in the queue!
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Idle);
    EXPECT_EQ(q_->shards[shard_idx].lookup_count, 1u);
}

TEST_F(WorkerQueueTest, ProcessingShardWithRemainingWorkRequeues) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    cfg.lookup_lane_quota = 1; // set quota to 1
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2LookupJob job1;
    std::strcpy(job1.key, "key-1");
    std::size_t shard_idx = expected_shard("key-1");

    // Collision key for same shard
    std::string key2;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "key-collision-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            key2 = candidate;
            break;
        }
    }
    ASSERT_FALSE(key2.empty());
    L2LookupJob job2;
    std::strcpy(job2.key, key2.c_str());

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job1));
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job2));

    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
    EXPECT_EQ(q_->worker_ready[0].count, 1u);

    // Run one event -> will execute 1 lookup job, leaving 1 job.
    // It should requeue: state remains Queued, ready queue count is still 1 (requeued)!
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
    EXPECT_EQ(q_->worker_ready[0].count, 1u);
}

TEST_F(WorkerQueueTest, ProcessingShardWithoutRemainingWorkReturnsIdle) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    L2LookupJob job1;
    std::strcpy(job1.key, "key-1");
    std::size_t shard_idx = expected_shard("key-1");

    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job1));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
    EXPECT_EQ(q_->worker_ready[0].count, 1u);

    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 0));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Idle);
    EXPECT_EQ(q_->worker_ready[0].count, 0u);
}

TEST_F(WorkerQueueTest, EnqueueWhileProcessingDoesNotPushDuplicateReadyEvent) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 1;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    std::size_t shard_idx = 5;
    // Set to Processing manually
    worker_queue_shard_set_state_for_test(q_.get(), shard_idx, RuntimeShardState::Processing);

    L2LookupJob job;
    std::strcpy(job.key, "some-key"); // We need a key that maps to shard_idx!
    std::string target_key;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "key-find-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            target_key = candidate;
            break;
        }
    }
    ASSERT_FALSE(target_key.empty());
    std::strcpy(job.key, target_key.c_str());

    // Enqueue
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));

    // Should not push to ready queue because it's in Processing state
    EXPECT_EQ(q_->worker_ready[0].count, 0u);
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx),
              RuntimeShardState::Processing);

    // Call requeue_or_clear_for_test -> sees work remains, transitions to Queued, pushes to ready
    // queue
    worker_queue_shard_requeue_or_clear_for_test(q_.get(), shard_idx);
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
    EXPECT_EQ(q_->worker_ready[0].count, 1u);
}

TEST_F(WorkerQueueTest, WrongWorkerReadyEventIsRejected) {
    WorkerQueueConfig cfg;
    cfg.worker_count = 2;
    ASSERT_EQ(worker_queue_init(q_.get(), cfg), nullptr);
    q_->running = true;

    // Shard 0 is owned by worker 0 (0 % 2 == 0)
    std::size_t shard_idx = 0;

    // Set shard state to Queued manually
    worker_queue_shard_set_state_for_test(q_.get(), shard_idx, RuntimeShardState::Queued);

    // Enqueue job into shard 0 manually so there is work
    L2LookupJob job;
    std::strcpy(job.key, "key-maps-to-shard-0"); // we can find one or just use one
    std::string key0;
    for (int i = 0; i < 1000; ++i) {
        std::string candidate = "key-find-" + std::to_string(i);
        if (expected_shard(candidate) == shard_idx) {
            key0 = candidate;
            break;
        }
    }
    ASSERT_FALSE(key0.empty());
    std::strcpy(job.key, key0.c_str());
    EXPECT_TRUE(worker_queue_try_enqueue_lookup(q_.get(), job));

    // Clear ready queue of worker 0 (since enqueue put it there)
    q_->worker_ready[0].head = 0;
    q_->worker_ready[0].tail = 0;
    q_->worker_ready[0].count = 0;

    // Manually push shard 0 into worker 1's ready queue
    {
        auto& rq1 = q_->worker_ready[1];
        std::lock_guard<std::mutex> lock(rq1.mu);
        rq1.shard_ids[rq1.tail] = shard_idx;
        rq1.tail = (rq1.tail + 1) % kRuntimeShardCount;
        rq1.count = 1;
    }

    // Run one event on worker 1
    // It should pop the event but reject it because worker 1 does not own shard 0!
    // The shard state should remain Queued (not transition to Processing or Idle), and job remains
    // in shard
    EXPECT_TRUE(worker_test_run_one_event(q_.get(), 1));
    EXPECT_EQ(worker_queue_shard_state_for_test(q_.get(), shard_idx), RuntimeShardState::Queued);
    EXPECT_EQ(q_->shards[shard_idx].lookup_count, 1u);
}

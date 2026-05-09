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

    // shard capacity is kRuntimeQueueSlotsPerShard
    for (std::size_t i = 0; i < kRuntimeQueueSlotsPerShard; ++i) {
        EXPECT_TRUE(worker_queue_try_enqueue_store(q_.get(), job)) << "Failed at index " << i;
    }

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
              q_->shards[found_shard].body_pool.heap_bodies[slot_idx]);
    EXPECT_NE(q_->shards[found_shard].store_slots[0].entry.body, original_body);
    EXPECT_STREQ(q_->shards[found_shard].body_pool.heap_bodies[slot_idx], original_body);
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
    EXPECT_EQ(q_->shards[1].lookup_count, 0u);

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
        EXPECT_FALSE(q_->shards[s].ready_enqueued);
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
}

TEST_F(WorkerQueueTest, LookupPrecedenceInTryPop) {
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

    // Now try pop. Even though store was enqueued FIRST, lookup must take precedence!
    DequeuedRuntimeJob popped_job;
    EXPECT_TRUE(worker_queue_shard_try_pop_for_test(q_.get(), shard_idx, &popped_job));
    EXPECT_EQ(popped_job.kind, DequeuedJobKind::Lookup);
    EXPECT_STREQ(popped_job.lookup_job.key, lookup_key.c_str());

    // Next pop should be the store job
    EXPECT_TRUE(worker_queue_shard_try_pop_for_test(q_.get(), shard_idx, &popped_job));
    EXPECT_EQ(popped_job.kind, DequeuedJobKind::Store);
    EXPECT_STREQ(popped_job.store_job.key, "common-shard-key");

    // Third pop should return false (empty)
    EXPECT_FALSE(worker_queue_shard_try_pop_for_test(q_.get(), shard_idx, &popped_job));
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
    DequeuedRuntimeJob popped_job;
    EXPECT_TRUE(worker_queue_shard_try_pop_for_test(q_.get(), shard_idx, &popped_job));

    // Requeue since 1 job remains
    worker_queue_shard_requeue_or_clear_for_test(q_.get(), shard_idx);
    EXPECT_TRUE(q_->shards[shard_idx].ready_enqueued);
    EXPECT_EQ(q_->worker_ready[0].count, 1u);
    EXPECT_EQ(q_->worker_ready[0].shard_ids[q_->worker_ready[0].head], shard_idx);

    // Pop the second job (now empty)
    EXPECT_TRUE(worker_queue_shard_try_pop_for_test(q_.get(), shard_idx, &popped_job));

    // Clear ready queue count again to isolate the next check
    q_->worker_ready[0].head = 0;
    q_->worker_ready[0].tail = 0;
    q_->worker_ready[0].count = 0;

    // Run requeue_or_clear (should clear ready_enqueued to false and NOT requeue)
    worker_queue_shard_requeue_or_clear_for_test(q_.get(), shard_idx);
    EXPECT_FALSE(q_->shards[shard_idx].ready_enqueued);
    EXPECT_EQ(q_->worker_ready[0].count, 0u);
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

    // Enqueue 5 store jobs to the same shard
    std::string key_base = "drain-test-key-";
    std::size_t target_shard = 0; // We'll map them to shard 0
    int enqueued_count = 0;

    for (int i = 0; i < 1000 && enqueued_count < 5; ++i) {
        std::string candidate = key_base + std::to_string(i);
        if (expected_shard(candidate) == target_shard) {
            L2StoreJob job;
            std::strcpy(job.key, candidate.c_str());
            job.body_len = 0;
            if (worker_queue_try_enqueue_store(q_.get(), job)) {
                enqueued_count++;
            }
        }
    }
    ASSERT_EQ(enqueued_count, 5);

    // Shutdown immediately. This will trigger the worker shutdown draining path.
    worker_queue_shutdown(q_.get());

    // Verify that all 5 store jobs were drained and processed from shard 0 during shutdown
    EXPECT_EQ(q_->shards[target_shard].store_count, 0u);
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
    EXPECT_LT(duration_ms, 5);
}

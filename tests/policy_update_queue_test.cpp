// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_update_queue.h"
#include "control_plane/policy_update_shard.h"
#include "control_plane/rocksdb_policy_state_store.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <gtest/gtest.h>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_queue_test_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

PolicyUpdateJob make_job(const std::string& job_id, const std::string& resource_key) {
    PolicyUpdateJob job;
    job.job_id = job_id;
    job.resource_key = resource_key;
    job.source_type = "yaml";
    return job;
}

} // namespace

TEST(PolicyUpdateQueueTest, StableShardIdForResourceKey) {
    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 256;
    PolicyUpdateQueue queue(config);

    const std::string key = "policy/default/runtime";
    const std::uint32_t shard_a = queue.compute_shard_id(key);
    const std::uint32_t shard_b = queue.compute_shard_id(key);
    EXPECT_EQ(shard_a, shard_b);
}

TEST(PolicyUpdateQueueTest, WorkerCountDoesNotAffectShardId) {
    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 128;
    PolicyUpdateQueue queue(config);

    const std::string key = "policy/default/runtime";
    const std::uint32_t shard = queue.compute_shard_id(key);
    EXPECT_LT(shard, config.logical_shard_count);
}

TEST(PolicyUpdateQueueTest, DifferentResourceKeysMayMapToDifferentShards) {
    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 256;
    PolicyUpdateQueue queue(config);

    const std::uint32_t shard_a = queue.compute_shard_id("policy/default/runtime-a");
    const std::uint32_t shard_b = queue.compute_shard_id("policy/default/runtime-b");
    EXPECT_NE(shard_a, shard_b);
}

TEST(PolicyUpdateQueueTest, EnqueueRejectsWithoutJobStore) {
    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 16;
    PolicyUpdateQueue queue(config);

    const auto enqueue_res = queue.enqueue(make_job("job-1", "policy/default/runtime"));
    EXPECT_FALSE(enqueue_res.ok);
    EXPECT_EQ(enqueue_res.error, "POLICY_JOB_STORE_FAILED");
}

TEST(PolicyUpdateQueueTest, JobTransitionsSubmittedToQueued) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 16;
    config.job_store = &store;
    PolicyUpdateQueue queue(config);

    const auto enqueue_res = queue.enqueue(make_job("job-1", "policy/default/runtime"));
    ASSERT_TRUE(enqueue_res.ok);
    EXPECT_EQ(queue.get_job_state("job-1"), PolicyUpdateJobState::Queued);

    destroy_db(db_path);
}

TEST(PolicyUpdateQueueTest, QueueFullRejectsJob) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 4;
    config.max_queue_depth_per_shard = 2;
    config.job_store = &store;
    PolicyUpdateQueue queue(config);

    const std::string key = "policy/default/runtime";
    const PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    ASSERT_TRUE(queue.enqueue(make_job("job-1", key)).ok);
    ASSERT_TRUE(queue.enqueue(make_job("job-2", key)).ok);
    const auto third = queue.enqueue(make_job("job-3", key));
    EXPECT_FALSE(third.ok);
    EXPECT_EQ(third.error, "POLICY_JOB_QUEUE_FULL");
    EXPECT_EQ(third.logical_shard_id, queue.compute_shard_id(key));
    EXPECT_EQ(third.message, "Policy update queue for this resource is full.");

    const auto loaded = store.load_policy_update_job(resource_key, "job-3");
    EXPECT_TRUE(loaded.not_found);

    destroy_db(db_path);
}

TEST(PolicyUpdateQueueTest, WorkerClaimsShardExclusively) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 8;
    config.job_store = &store;
    PolicyUpdateQueue queue(config);
    ASSERT_TRUE(queue.enqueue(make_job("job-1", "policy/default/runtime")).ok);

    PolicyUpdateShard* first = nullptr;
    PolicyUpdateShard* second = nullptr;
    std::mutex claim_mu;
    std::condition_variable claim_cv;
    int claims = 0;

    auto claim = [&]() {
        PolicyUpdateShard* shard = queue.try_claim_shard();
        if (shard != nullptr) {
            std::lock_guard<std::mutex> lock(claim_mu);
            if (first == nullptr) {
                first = shard;
            } else {
                second = shard;
            }
            ++claims;
            claim_cv.notify_all();
        }
    };

    std::thread t1(claim);
    std::thread t2(claim);

    {
        std::unique_lock<std::mutex> lock(claim_mu);
        claim_cv.wait_for(lock, std::chrono::seconds(2), [&]() { return claims >= 1; });
    }

    t1.join();
    t2.join();

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(second, nullptr);
    queue.release_shard(first);

    destroy_db(db_path);
}

TEST(PolicyUpdateQueueTest, SameShardSerializesJobs) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 8;
    config.job_store = &store;
    PolicyUpdateQueue queue(config);
    const std::string key = "policy/default/runtime";

    ASSERT_TRUE(queue.enqueue(make_job("job-1", key)).ok);
    ASSERT_TRUE(queue.enqueue(make_job("job-2", key)).ok);

    PolicyUpdateShard* shard = queue.try_claim_shard();
    ASSERT_NE(shard, nullptr);

    std::size_t first_batch = 0;
    {
        std::lock_guard<std::mutex> lock(shard->mu);
        first_batch = shard->jobs.size();
        EXPECT_EQ(shard->state, PolicyUpdateShardState::Processing);
    }
    EXPECT_GE(first_batch, 1u);

    queue.release_shard(shard);

    destroy_db(db_path);
}

TEST(PolicyUpdateQueueTest, ToStringShardStateIncludesDraining) {
    EXPECT_STREQ(to_string(PolicyUpdateShardState::Draining), "draining");
}

TEST(PolicyUpdateQueueTest, BeginDrainingRejectsNewEnqueue) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 16;
    config.job_store = &store;
    PolicyUpdateQueue queue(config);

    const std::string key = "policy/default/runtime";
    ASSERT_TRUE(queue.enqueue(make_job("job-1", key)).ok);
    queue.begin_draining();

    const auto second = queue.enqueue(make_job("job-2", key));
    EXPECT_FALSE(second.ok);
    EXPECT_EQ(second.error, "POLICY_JOB_QUEUE_DRAINING");
    EXPECT_EQ(second.logical_shard_id, queue.compute_shard_id(key));

    destroy_db(db_path);
}

TEST(PolicyUpdateQueueTest, DrainingShardProcessesRemainingJobs) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyUpdateQueueConfig config;
    config.logical_shard_count = 8;
    config.job_store = &store;
    PolicyUpdateQueue queue(config);
    const std::string key = "policy/default/runtime";

    ASSERT_TRUE(queue.enqueue(make_job("job-1", key)).ok);
    ASSERT_TRUE(queue.enqueue(make_job("job-2", key)).ok);
    queue.begin_draining();

    PolicyUpdateShard* shard = queue.try_claim_shard();
    ASSERT_NE(shard, nullptr);
    EXPECT_EQ(shard->state, PolicyUpdateShardState::Processing);

    {
        std::lock_guard<std::mutex> lock(shard->mu);
        EXPECT_EQ(shard->jobs.size(), 2u);
        shard->jobs.pop_front();
        shard->jobs.pop_front();
    }

    queue.release_shard(shard);
    EXPECT_EQ(shard->state, PolicyUpdateShardState::Idle);

    destroy_db(db_path);
}

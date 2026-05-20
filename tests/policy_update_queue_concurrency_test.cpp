// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_metrics.h"
#include "control_plane/policy_apply_transaction.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_update_queue.h"
#include "control_plane/policy_update_worker.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <mutex>
#include <random>
#include <rocksdb/db.h>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_queue_cc_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    rocksdb::DestroyDB(path, rocksdb::Options{});
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation,
                                 const std::string& expected_base_sha = "") {
    TqPolicyDocument doc;
    doc.document_id = "queue-cc-test";
    doc.source_name = "test";
    doc.expected_base_sha = expected_base_sha;
    doc.version.source_schema_version = "yaml/v1";
    doc.version.policy_ir_version = "tq-ir/v1";
    doc.version.identity_version = "policy-identity/v2";
    doc.generation = generation;
    doc.schema_version_num = 1;
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";

    TqRoutePolicy route;
    route.route_id = route_id;
    route.match_prefix = "/api";
    route.match_kind = TqRouteMatchKind::Prefix;
    route.mutation = TqMutationMode::Disabled;
    route.allowed_method = TqHttpMethod::Get;
    doc.routes.push_back(route);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

std::string canonical_hash_for_yaml(const std::string& yaml) {
    return "sha256:" + compute_canonical_yaml_sha256_hex(yaml);
}

void seed_active_policy(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                        const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    PolicyVersionRecord version;
    version.generation = generation;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    version.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    ActivePolicyPointer pointer;
    pointer.generation = generation;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = version.canonical_hash;
    pointer.version_key = make_version_key(key, generation);
    pointer.yaml_key = make_yaml_key(key, generation);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = generation > 1 ? generation - 1 : 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

PolicyUpdateJob make_yaml_job(const std::string& job_id, const std::string& resource_key,
                              const TqPolicyDocument& base, const std::string& candidate_yaml) {
    PolicyUpdateJob job;
    job.job_id = job_id;
    job.resource_key = resource_key;
    job.source_type = "yaml";
    job.expected_base_generation = 1;
    job.expected_base_policy_id = base.policy_id;
    job.apply_request.source = candidate_yaml;
    job.apply_request.mode = TqApplyMode::Apply;
    job.apply_request.expected_base_identity = base.policy_id;
    job.apply_request.strict_production = true;
    return job;
}

bool wait_for_job_state(PolicyUpdateQueue& queue, const std::string& job_id,
                        PolicyUpdateJobState target, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const PolicyUpdateJobState state = queue.get_job_state(job_id);
        if (state == target) {
            return true;
        }
        if (state == PolicyUpdateJobState::Failed) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return queue.get_job_state(job_id) == target;
}

} // namespace

class PolicyUpdateQueueConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db(db_path_);
        store_.emplace(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        key_ = PolicyResourceKey::default_runtime();

        PolicyUpdateQueueConfig queue_config;
        queue_config.logical_shard_count = 32;
        queue_config.job_store = &*store_;
        queue_ = std::make_unique<PolicyUpdateQueue>(queue_config);

        tx_config_.policy_state_store = &*store_;
        tx_config_.resource_key = key_;
    }

    void TearDown() override {
        stop_workers();
        queue_.reset();
        store_.reset();
        destroy_db(db_path_);
    }

    void start_workers(std::size_t count) {
        workers_.clear();
        for (std::size_t i = 0; i < count; ++i) {
            auto worker = std::make_unique<PolicyUpdateWorker>(static_cast<std::uint32_t>(i),
                                                               queue_.get(), tx_config_);
            worker->start();
            workers_.push_back(std::move(worker));
        }
        for (const auto& worker : workers_) {
            while (!worker->is_running()) {
                std::this_thread::yield();
            }
        }
    }

    void stop_workers() {
        for (auto& worker : workers_) {
            if (worker != nullptr) {
                worker->stop();
            }
        }
        workers_.clear();
    }

    std::string db_path_;
    PolicyResourceKey key_;
    std::optional<RocksDBPolicyStateStore> store_;
    std::unique_ptr<PolicyUpdateQueue> queue_;
    PolicyApplyTransactionConfig tx_config_;
    std::vector<std::unique_ptr<PolicyUpdateWorker>> workers_;
};

TEST_F(PolicyUpdateQueueConcurrencyTest, WorkerProcessesEnqueuedJob) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);
    start_workers(1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(emit.ok);

    PolicyUpdateJob job = make_yaml_job("job-worker", key_.to_string(), base, emit.yaml);
    const EnqueueJobResult enqueue_res = queue_->enqueue(std::move(job));
    ASSERT_TRUE(enqueue_res.ok);

    ASSERT_TRUE(wait_for_job_state(*queue_, enqueue_res.job_id, PolicyUpdateJobState::Committed,
                                   std::chrono::seconds(10)));
}

TEST_F(PolicyUpdateQueueConcurrencyTest, SameResourceSerializesAcrossWorkers) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    start_workers(4);

    const std::string resource_key = key_.to_string();
    std::uint64_t expected_generation = 1;
    std::string expected_policy_id = base.policy_id;

    for (int i = 0; i < 6; ++i) {
        TqPolicyDocument candidate = make_policy_doc("route-" + std::to_string(i),
                                                     expected_generation + 1, expected_policy_id);
        PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(candidate);
        ASSERT_TRUE(emit.ok);

        PolicyUpdateJob job =
            make_yaml_job("job-serial-" + std::to_string(i), resource_key, base, emit.yaml);
        job.expected_base_generation = expected_generation;
        job.expected_base_policy_id = expected_policy_id;
        job.apply_request.expected_base_identity = expected_policy_id;

        const EnqueueJobResult enqueue_res = queue_->enqueue(std::move(job));
        ASSERT_TRUE(enqueue_res.ok);

        ASSERT_TRUE(wait_for_job_state(*queue_, enqueue_res.job_id, PolicyUpdateJobState::Committed,
                                       std::chrono::seconds(10)));

        const auto active = store_->load_active_pointer(key_);
        ASSERT_TRUE(active.ok);
        expected_generation = active.pointer.generation;
        expected_policy_id = active.pointer.policy_id;
        base = candidate;
        base.policy_id = expected_policy_id;
    }

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 7u);
}

TEST_F(PolicyUpdateQueueConcurrencyTest, DifferentResourcesApplyConcurrently) {
    std::atomic<int> concurrent_peak{ 0 };
    std::atomic<int> currently_running{ 0 };
    tx_config_.on_state_change = [&](const PolicyUpdateJob& job) {
        if (job.state == PolicyUpdateJobState::Processing) {
            const int running = currently_running.fetch_add(1) + 1;
            int peak = concurrent_peak.load(std::memory_order_relaxed);
            while (running > peak && !concurrent_peak.compare_exchange_weak(
                                         peak, running, std::memory_order_relaxed)) {
            }
        } else if (job.state == PolicyUpdateJobState::Committed ||
                   job.state == PolicyUpdateJobState::Failed) {
            currently_running.fetch_sub(1);
        }
    };

    start_workers(4);

    std::vector<std::string> resource_keys;
    for (int i = 0; i < 8; ++i) {
        resource_keys.push_back("policy/default/runtime-" + std::to_string(i));
    }

    std::vector<std::string> job_ids;
    for (std::size_t i = 0; i < resource_keys.size(); ++i) {
        PolicyResourceKey resource_key;
        ASSERT_TRUE(parse_resource_key(resource_keys[i], &resource_key));

        TqPolicyDocument base = make_policy_doc("route-base-" + std::to_string(i), 1);
        seed_active_policy(*store_, resource_key, base, 1);

        TqPolicyDocument candidate =
            make_policy_doc("route-candidate-" + std::to_string(i), 2, base.policy_id);
        PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(candidate);
        ASSERT_TRUE(emit.ok);

        PolicyUpdateJob job =
            make_yaml_job("job-parallel-" + std::to_string(i), resource_keys[i], base, emit.yaml);
        const EnqueueJobResult enqueue_res = queue_->enqueue(std::move(job));
        ASSERT_TRUE(enqueue_res.ok);
        job_ids.push_back(enqueue_res.job_id);
    }

    for (const std::string& job_id : job_ids) {
        ASSERT_TRUE(wait_for_job_state(*queue_, job_id, PolicyUpdateJobState::Committed,
                                       std::chrono::seconds(10)))
            << job_id;
    }

    EXPECT_GT(concurrent_peak.load(), 1)
        << "expected at least two jobs in processing state simultaneously";
}

TEST_F(PolicyUpdateQueueConcurrencyTest, FifoOrderPerResourceWithOneWorker) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    start_workers(1);

    const std::string resource_key = key_.to_string();
    std::uint64_t expected_generation = 1;
    std::string expected_policy_id = base.policy_id;

    for (int i = 0; i < 4; ++i) {
        TqPolicyDocument candidate = make_policy_doc("route-fifo-" + std::to_string(i),
                                                     expected_generation + 1, expected_policy_id);
        PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(candidate);
        ASSERT_TRUE(emit.ok);

        PolicyUpdateJob job =
            make_yaml_job("job-fifo-" + std::to_string(i), resource_key, base, emit.yaml);
        job.expected_base_generation = expected_generation;
        job.expected_base_policy_id = expected_policy_id;
        job.apply_request.expected_base_identity = expected_policy_id;

        const EnqueueJobResult enqueue_res = queue_->enqueue(std::move(job));
        ASSERT_TRUE(enqueue_res.ok);

        ASSERT_TRUE(wait_for_job_state(*queue_, enqueue_res.job_id, PolicyUpdateJobState::Committed,
                                       std::chrono::seconds(10)));

        const auto active = store_->load_active_pointer(key_);
        ASSERT_TRUE(active.ok);
        EXPECT_EQ(active.pointer.generation, expected_generation + 1);

        expected_generation = active.pointer.generation;
        expected_policy_id = active.pointer.policy_id;
        base = candidate;
        base.policy_id = expected_policy_id;
    }
}

TEST_F(PolicyUpdateQueueConcurrencyTest, FifoOrderPerResourceWithFourWorkers) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    start_workers(4);

    const std::string resource_key = key_.to_string();
    std::vector<std::uint64_t> observed_generations;

    std::uint64_t expected_generation = 1;
    std::string expected_policy_id = base.policy_id;

    for (int i = 0; i < 4; ++i) {
        TqPolicyDocument candidate = make_policy_doc("route-fifo4-" + std::to_string(i),
                                                     expected_generation + 1, expected_policy_id);
        PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(candidate);
        ASSERT_TRUE(emit.ok);

        PolicyUpdateJob job =
            make_yaml_job("job-fifo4-" + std::to_string(i), resource_key, base, emit.yaml);
        job.expected_base_generation = expected_generation;
        job.expected_base_policy_id = expected_policy_id;
        job.apply_request.expected_base_identity = expected_policy_id;

        const EnqueueJobResult enqueue_res = queue_->enqueue(std::move(job));
        ASSERT_TRUE(enqueue_res.ok);

        ASSERT_TRUE(wait_for_job_state(*queue_, enqueue_res.job_id, PolicyUpdateJobState::Committed,
                                       std::chrono::seconds(10)));

        const auto active = store_->load_active_pointer(key_);
        ASSERT_TRUE(active.ok);
        observed_generations.push_back(active.pointer.generation);
        expected_generation = active.pointer.generation;
        expected_policy_id = active.pointer.policy_id;
        base = candidate;
        base.policy_id = expected_policy_id;
    }

    ASSERT_EQ(observed_generations.size(), 4u);
    for (std::size_t i = 0; i < observed_generations.size(); ++i) {
        EXPECT_EQ(observed_generations[i], 2u + i);
    }
}

TEST(PolicyUpdateWorkerTest, StartStopUpdatesActiveWorkerMetric) {
    ControlPlaneMetrics metrics{};
    PolicyUpdateQueueConfig queue_config;
    queue_config.logical_shard_count = 4;
    PolicyUpdateQueue queue(queue_config);

    PolicyApplyTransactionConfig tx_config{};
    tx_config.control_plane_metrics = &metrics;

    PolicyUpdateWorker worker(0, &queue, tx_config);
    EXPECT_EQ(metrics.policy_update_worker_active.load(), 0u);
    worker.start();
    EXPECT_EQ(metrics.policy_update_worker_active.load(), 1u);
    worker.stop();
    EXPECT_EQ(metrics.policy_update_worker_active.load(), 0u);
}

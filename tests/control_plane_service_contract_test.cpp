// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_config.h"
#include "control_plane/control_plane_service.h"
#include "control_plane/policy_apply_api.h"
#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_apply_transaction.h"
#include "control_plane/policy_update_queue.h"
#include "control_plane/policy_update_worker.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
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
    return (fs::temp_directory_path() / ("bt_cp_svc_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    rocksdb::DestroyDB(path, rocksdb::Options{});
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation,
                                 const std::string& expected_base_sha = "") {
    TqPolicyDocument doc;
    doc.document_id = "cp-svc-test";
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

} // namespace

class ControlPlaneServiceContractTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db(db_path_);
        store_.emplace(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        key_ = PolicyResourceKey::default_runtime();

        config_.policy_state_store = &*store_;

        PolicyUpdateQueueConfig queue_config;
        queue_config.logical_shard_count = 16;
        queue_config.job_store = &*store_;
        queue_ = std::make_unique<PolicyUpdateQueue>(queue_config);
        config_.policy_update_queue = queue_.get();

        tx_config_.policy_state_store = &*store_;
        tx_config_.resource_key = key_;

        service_ = std::make_unique<ControlPlaneService>(config_);
    }

    void TearDown() override {
        stop_workers();
        service_.reset();
        queue_.reset();
        store_.reset();
        destroy_db(db_path_);
    }

    void start_workers(std::size_t count = 1) {
        workers_.clear();
        for (std::size_t i = 0; i < count; ++i) {
            auto worker = std::make_unique<PolicyUpdateWorker>(static_cast<std::uint32_t>(i),
                                                               queue_.get(), tx_config_);
            worker->start();
            workers_.push_back(std::move(worker));
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

    bool wait_for_job_state(const std::string& job_id, PolicyUpdateJobState target) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            const PolicyUpdateJobState state = queue_->get_job_state(job_id);
            if (state == target) {
                return true;
            }
            if (state == PolicyUpdateJobState::Failed) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return queue_->get_job_state(job_id) == target;
    }

    bool collect_job_states_until(const std::string& job_id, PolicyUpdateJobState terminal,
                                  std::vector<PolicyUpdateJobState>* observed) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            const PolicyUpdateJobState state = queue_->get_job_state(job_id);
            if (observed != nullptr) {
                if (observed->empty() || observed->back() != state) {
                    observed->push_back(state);
                }
            }
            if (state == terminal) {
                return true;
            }
            if (state == PolicyUpdateJobState::Failed) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return queue_->get_job_state(job_id) == terminal;
    }

    std::string db_path_;
    PolicyResourceKey key_;
    ControlPlaneServiceConfig config_;
    PolicyApplyTransactionConfig tx_config_;
    std::optional<RocksDBPolicyStateStore> store_;
    std::unique_ptr<PolicyUpdateQueue> queue_;
    std::vector<std::unique_ptr<PolicyUpdateWorker>> workers_;
    std::unique_ptr<ControlPlaneService> service_;
};

TEST_F(ControlPlaneServiceContractTest, DryRunRejectsEmptySource) {
    PolicyDryRunRequest request{};
    request.source = "";
    const auto result = service_->dry_run(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedInvalidRequest);
}

TEST_F(ControlPlaneServiceContractTest, DryRunRejectsInvalidSourceType) {
    PolicyDryRunRequest request{};
    request.source = "routes: []\n";
    request.source_type = static_cast<PolicyApplySourceType>(99);
    const auto result = service_->dry_run(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedInvalidRequest);
}

TEST_F(ControlPlaneServiceContractTest, DryRunReadyWithYamlCandidate) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyDryRunRequest request{};
    request.resource_key = key_;
    request.source_type = PolicyApplySourceType::Yaml;
    request.source = candidate_emit.yaml;
    request.expected_base_generation = 1;
    request.expected_base_policy_id = base.policy_id;
    request.strict_production = true;

    const auto result = service_->dry_run(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::DryRunReady);
    EXPECT_EQ(result.base_policy_id, base.policy_id);
    EXPECT_NE(result.candidate_policy_id, base.policy_id);
    EXPECT_EQ(result.plan.added_routes, 1);
    EXPECT_EQ(result.plan.removed_routes, 1);
    EXPECT_EQ(result.plan.modified_routes, 0);
}

TEST_F(ControlPlaneServiceContractTest, DiffReadyWithYamlCandidate) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyDiffRequest request{};
    request.resource_key = key_;
    request.source_type = PolicyApplySourceType::Yaml;
    request.source = candidate_emit.yaml;
    request.expected_base_generation = 1;
    request.expected_base_policy_id = base.policy_id;
    request.strict_production = true;

    const auto result = service_->diff(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::DiffReady);
    EXPECT_EQ(result.plan.added_routes, 1);
    EXPECT_EQ(result.plan.removed_routes, 1);
}

TEST_F(ControlPlaneServiceContractTest, ApplyRequiresExpectedBaseFields) {
    PolicyApplyRequest request{};
    request.source = "routes: []\n";
    request.expected_base_generation = 0;
    request.expected_base_policy_id = "";
    const auto result = service_->apply(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedInvalidRequest);
}

TEST_F(ControlPlaneServiceContractTest, ApplyRejectsStaleExpectedBase) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyApplyRequest request{};
    request.resource_key = key_;
    request.source_type = PolicyApplySourceType::Yaml;
    request.source = candidate_emit.yaml;
    request.expected_base_generation = 99;
    request.expected_base_policy_id = base.policy_id;

    const auto result = service_->apply(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedCasMismatch);
    EXPECT_EQ(result.actual_base_generation, 1u);
    EXPECT_EQ(result.actual_base_policy_id, base.policy_id);
    EXPECT_EQ(result.expected_base_generation, 99u);
}

TEST_F(ControlPlaneServiceContractTest, ApplyJobObservesSubmittedQueuedProcessingCommitted) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    std::vector<PolicyUpdateJobState> lifecycle_states;
    std::mutex lifecycle_mu;
    tx_config_.on_state_change = [&lifecycle_states, &lifecycle_mu](const PolicyUpdateJob& job) {
        std::lock_guard<std::mutex> lock(lifecycle_mu);
        if (lifecycle_states.empty() || lifecycle_states.back() != job.state) {
            lifecycle_states.push_back(job.state);
        }
    };

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyApplyRequest request{};
    request.resource_key = key_;
    request.source_type = PolicyApplySourceType::Yaml;
    request.source = candidate_emit.yaml;
    request.expected_base_generation = 1;
    request.expected_base_policy_id = base.policy_id;
    request.request_id = "req-job-states-001";

    const auto result = service_->apply(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(queue_->get_job_state(result.job_id), PolicyUpdateJobState::Queued);

    start_workers(1);
    ASSERT_TRUE(wait_for_job_state(result.job_id, PolicyUpdateJobState::Committed));

    auto seen = [&](PolicyUpdateJobState state) {
        return std::find(lifecycle_states.begin(), lifecycle_states.end(), state) !=
               lifecycle_states.end();
    };
    EXPECT_TRUE(seen(PolicyUpdateJobState::Processing));
    EXPECT_TRUE(seen(PolicyUpdateJobState::Committed));
}

TEST_F(ControlPlaneServiceContractTest, ApplyAcceptedWithValidCas) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);
    start_workers(1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyApplyRequest request{};
    request.resource_key = key_;
    request.source_type = PolicyApplySourceType::Yaml;
    request.source = candidate_emit.yaml;
    request.expected_base_generation = 1;
    request.expected_base_policy_id = base.policy_id;
    request.request_id = "req-apply-001";

    const auto result = service_->apply(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::Accepted);
    EXPECT_FALSE(result.job_id.empty());
    EXPECT_EQ(result.job_id, "policy-job-req-apply-001");
    EXPECT_EQ(result.logical_shard_id, queue_->compute_shard_id(key_.to_string()));

    ASSERT_TRUE(wait_for_job_state(result.job_id, PolicyUpdateJobState::Committed));

    const auto job_query = service_->get_policy_update_job(result.job_id, key_);
    ASSERT_TRUE(job_query.ok) << job_query.error;
    EXPECT_EQ(job_query.job.state, "committed");
    EXPECT_EQ(job_query.job.job_id, result.job_id);
    EXPECT_EQ(job_query.job.logical_shard_id, result.logical_shard_id);

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 2u);
    EXPECT_NE(active.pointer.policy_id, base.policy_id);
}

TEST_F(ControlPlaneServiceContractTest, ApplyRejectsQueueFull) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    PolicyUpdateQueueConfig queue_config;
    queue_config.logical_shard_count = 16;
    queue_config.max_queue_depth_per_shard = 1;
    queue_config.job_store = &*store_;
    queue_ = std::make_unique<PolicyUpdateQueue>(queue_config);
    config_.policy_update_queue = queue_.get();
    service_ = std::make_unique<ControlPlaneService>(config_);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyApplyRequest first{};
    first.resource_key = key_;
    first.source_type = PolicyApplySourceType::Yaml;
    first.source = candidate_emit.yaml;
    first.expected_base_generation = 1;
    first.expected_base_policy_id = base.policy_id;
    first.request_id = "req-queue-full-1";
    ASSERT_TRUE(service_->apply(first).ok);

    PolicyApplyRequest second = first;
    second.request_id = "req-queue-full-2";
    const auto result = service_->apply(second);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedQueueFull);
    EXPECT_EQ(result.error, "POLICY_JOB_QUEUE_FULL");
    EXPECT_EQ(result.logical_shard_id, queue_->compute_shard_id(key_.to_string()));
    EXPECT_EQ(result.message, "Policy update queue for this resource is full.");

    const auto orphan = store_->load_policy_update_job(key_, "policy-job-req-queue-full-2");
    EXPECT_TRUE(orphan.not_found);
}

TEST_F(ControlPlaneServiceContractTest, ApplyRejectsWithoutQueueJobStore) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    PolicyUpdateQueueConfig queue_config;
    queue_config.logical_shard_count = 16;
    queue_ = std::make_unique<PolicyUpdateQueue>(queue_config);
    config_.policy_update_queue = queue_.get();
    service_ = std::make_unique<ControlPlaneService>(config_);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyApplyRequest request{};
    request.resource_key = key_;
    request.source_type = PolicyApplySourceType::Yaml;
    request.source = candidate_emit.yaml;
    request.expected_base_generation = 1;
    request.expected_base_policy_id = base.policy_id;
    request.request_id = "req-no-job-store";

    const auto result = service_->apply(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedStorageUnavailable);
    EXPECT_EQ(result.error, "policy update queue job store is not configured");
}

TEST_F(ControlPlaneServiceContractTest, ApplyRejectsWhenPolicyInactive) {
    config_.is_policy_inactive = [](const PolicyResourceKey&) { return true; };
    service_ = std::make_unique<ControlPlaneService>(config_);

    PolicyApplyRequest request{};
    request.source = "routes: []\n";
    request.expected_base_generation = 1;
    request.expected_base_policy_id = "sha256:abc";
    const auto result = service_->apply(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedPolicyInactive);
}

TEST_F(ControlPlaneServiceContractTest, ActivePolicyQueryReturnsPointer) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    const auto result = service_->get_active_policy(key_);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.active.generation, 1u);
    EXPECT_EQ(result.active.policy_id, base.policy_id);
}

TEST_F(ControlPlaneServiceContractTest, ActivePolicyQueryMissingPointer) {
    const auto result = service_->get_active_policy(key_);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedInvalidRequest);
}

TEST_F(ControlPlaneServiceContractTest, RollbackPlanRejectsMissingTargetGeneration) {
    PolicyRollbackPlanRequest request{};
    request.target_generation = 0;
    const auto result = service_->plan_rollback(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedInvalidRequest);
}

TEST_F(ControlPlaneServiceContractTest, RollbackPlanReadyWhenTargetExists) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    PolicyRollbackPlanRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    const auto result = service_->plan_rollback(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::RollbackPlanReady);
    EXPECT_EQ(result.current_generation, 1u);
    EXPECT_EQ(result.target_generation, 1u);
    EXPECT_EQ(result.would_create_generation, 2u);
    EXPECT_EQ(result.target_policy_id, base.policy_id);
}

TEST(ControlPlaneServiceContract, StatusToStringCoversKnownValues) {
    EXPECT_STREQ(to_string(PolicyApplyStatus::DryRunReady), "DryRunReady");
    EXPECT_STREQ(to_string(PolicyApplyStatus::RejectedCasMismatch), "RejectedCasMismatch");
    EXPECT_STREQ(to_string(PolicyApplyStatus::RejectedQueueFull), "RejectedQueueFull");
    EXPECT_STREQ(to_string(PolicyApplyStatus::Accepted), "Accepted");
    EXPECT_STREQ(to_string(PolicyApplyStatus::RollbackPlanReady), "RollbackPlanReady");
}

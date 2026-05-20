// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_apply_transaction.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <rocksdb/db.h>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_tx_test_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    rocksdb::DestroyDB(path, rocksdb::Options{});
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation,
                                 const std::string& expected_base_sha = "") {
    TqPolicyDocument doc;
    doc.document_id = "tx-test";
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

class PromoteConflictPolicyStateStore : public PolicyStateStore {
public:
    explicit PromoteConflictPolicyStateStore(PolicyStateStore* inner) : inner_(inner) {}

    StorePolicyVersionResult store_policy_version(const PolicyResourceKey& key,
                                                  const PolicyVersionRecord& version,
                                                  const std::string& canonical_yaml) override {
        return inner_->store_policy_version(key, version, canonical_yaml);
    }

    LoadPolicyVersionResult load_policy_version(const PolicyResourceKey& key,
                                                std::uint64_t generation) override {
        return inner_->load_policy_version(key, generation);
    }

    LoadActivePointerResult load_active_pointer(const PolicyResourceKey& key) override {
        return inner_->load_active_pointer(key);
    }

    PromoteActiveResult compare_and_promote_active(const PolicyResourceKey& key,
                                                   const ExpectedActivePolicy& expected,
                                                   const ActivePolicyPointer& next) override {
        (void) key;
        (void) expected;
        (void) next;
        PromoteActiveResult res;
        res.ok = false;
        res.code = PolicyStateErrorCode::ActivePointerConflict;
        res.error = "active policy pointer conflict (ACTIVE_POINTER_CONFLICT)";
        return res;
    }

    CommitPolicyGenerationWithAuditResult commit_policy_generation_with_audit(
        const CommitPolicyGenerationWithAuditParams& params) override {
        return inner_->commit_policy_generation_with_audit(params);
    }

    AppendAuditResult append_audit_record(const PolicyResourceKey& key,
                                          const PolicyAuditRecord& record) override {
        return inner_->append_audit_record(key, record);
    }

    StorePolicyUpdateJobResult store_policy_update_job(const PolicyResourceKey& key,
                                                       const PolicyUpdateJobRecord& job) override {
        return inner_->store_policy_update_job(key, job);
    }

    LoadPolicyUpdateJobResult load_policy_update_job(const PolicyResourceKey& key,
                                                     const std::string& job_id) override {
        return inner_->load_policy_update_job(key, job_id);
    }

private:
    PolicyStateStore* inner_;
};

PolicyUpdateJob make_apply_job(const PolicyResourceKey& key, const TqPolicyDocument& base,
                               const std::string& candidate_yaml, const std::string& job_id) {
    PolicyUpdateJob job;
    job.job_id = job_id;
    job.resource_key = key.to_string();
    job.source_type = "yaml";
    job.expected_base_generation = 1;
    job.expected_base_policy_id = base.policy_id;
    job.apply_request.source = candidate_yaml;
    job.apply_request.mode = TqApplyMode::Apply;
    job.apply_request.expected_base_identity = base.policy_id;
    job.apply_request.strict_production = true;
    return job;
}

} // namespace

class PolicyApplyTransactionTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db(db_path_);
        store_.emplace(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        key_ = PolicyResourceKey::default_runtime();
        tx_config_.policy_state_store = &*store_;
        tx_config_.resource_key = key_;
    }

    void TearDown() override {
        store_.reset();
        destroy_db(db_path_);
    }

    std::string db_path_;
    PolicyResourceKey key_;
    std::optional<RocksDBPolicyStateStore> store_;
    PolicyApplyTransactionConfig tx_config_;
};

TEST_F(PolicyApplyTransactionTest, SuccessfulTransactionStoresVersionThenPromotes) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyUpdateJob job = make_apply_job(key_, base, candidate_emit.yaml, "job-success");
    PolicyApplyTransaction transaction(tx_config_);
    const PolicyApplyTransactionResult result = transaction.execute(job);

    ASSERT_TRUE(result.ok) << result.error_message;
    EXPECT_EQ(result.final_state, PolicyUpdateJobState::Committed);
    EXPECT_EQ(job.state, PolicyUpdateJobState::Committed);

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 2u);
    EXPECT_EQ(active.pointer.policy_id, result.candidate_policy_id);

    const auto version = store_->load_policy_version(key_, 2);
    ASSERT_TRUE(version.ok);
    EXPECT_EQ(version.record.policy_id, result.candidate_policy_id);
}

TEST_F(PolicyApplyTransactionTest, BaseMismatchFails) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyUpdateJob job = make_apply_job(key_, base, candidate_emit.yaml, "job-base-mismatch");
    job.expected_base_generation = 99;

    PolicyApplyTransaction transaction(tx_config_);
    const PolicyApplyTransactionResult result = transaction.execute(job);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, "POLICY_APPLY_BASE_MISMATCH");
    EXPECT_EQ(result.failure_stage, PolicyApplyStage::ValidateBase);
}

TEST_F(PolicyApplyTransactionTest, CasMismatchFailsAtCompareAndPromote) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PromoteConflictPolicyStateStore conflict_store(&*store_);
    PolicyApplyTransactionConfig conflict_tx = tx_config_;
    conflict_tx.policy_state_store = &conflict_store;

    PolicyUpdateJob job = make_apply_job(key_, base, candidate_emit.yaml, "job-promote-conflict");
    PolicyApplyTransaction transaction(conflict_tx);
    const PolicyApplyTransactionResult result = transaction.execute(job);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, "POLICY_APPLY_PROMOTE_CONFLICT");
    EXPECT_EQ(result.failure_stage, PolicyApplyStage::CompareAndPromote);
    EXPECT_EQ(job.failure.stage, "compare_and_promote");
    EXPECT_EQ(job.failure.code, "POLICY_APPLY_PROMOTE_CONFLICT");

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 1u);
    EXPECT_EQ(active.pointer.policy_id, base.policy_id);

    const auto version = store_->load_policy_version(key_, 2);
    ASSERT_TRUE(version.ok);
}

TEST_F(PolicyApplyTransactionTest, CompareAndPromoteRejectsStaleExpectedAtStore) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument gen2 = make_policy_doc("route-gen2", 2, base.policy_id);
    PolicyIrYamlEmitResult gen2_emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(gen2_emit.ok);

    PolicyVersionRecord version2;
    version2.generation = 2;
    version2.policy_id = gen2.policy_id;
    version2.schema_version = 1;
    version2.api_version = gen2.api_version;
    version2.kind = gen2.kind;
    version2.source_type = "taperql-apply";
    version2.canonical_hash = canonical_hash_for_yaml(gen2_emit.yaml);
    ASSERT_TRUE(store_->store_policy_version(key_, version2, gen2_emit.yaml).ok);

    ActivePolicyPointer pointer2;
    pointer2.generation = 2;
    pointer2.policy_id = gen2.policy_id;
    pointer2.canonical_hash = version2.canonical_hash;
    pointer2.version_key = make_version_key(key_, 2);
    pointer2.yaml_key = make_yaml_key(key_, 2);
    pointer2.schema_version = 1;
    pointer2.api_version = gen2.api_version;
    pointer2.kind = gen2.kind;
    pointer2.source_type = "taperql-apply";

    ExpectedActivePolicy expected1;
    expected1.generation = 1;
    expected1.policy_id = base.policy_id;
    ASSERT_TRUE(store_->compare_and_promote_active(key_, expected1, pointer2).ok);

    TqPolicyDocument gen3 = make_policy_doc("route-gen3", 3, gen2.policy_id);
    PolicyIrYamlEmitResult gen3_emit = emit_policy_ir_canonical_yaml(gen3);
    ASSERT_TRUE(gen3_emit.ok);

    PolicyVersionRecord version3;
    version3.generation = 3;
    version3.policy_id = gen3.policy_id;
    version3.schema_version = 1;
    version3.api_version = gen3.api_version;
    version3.kind = gen3.kind;
    version3.source_type = "taperql-apply";
    version3.canonical_hash = canonical_hash_for_yaml(gen3_emit.yaml);
    ASSERT_TRUE(store_->store_policy_version(key_, version3, gen3_emit.yaml).ok);

    ActivePolicyPointer pointer3;
    pointer3.generation = 3;
    pointer3.policy_id = gen3.policy_id;
    pointer3.canonical_hash = version3.canonical_hash;
    pointer3.version_key = make_version_key(key_, 3);
    pointer3.yaml_key = make_yaml_key(key_, 3);
    pointer3.schema_version = 1;
    pointer3.api_version = gen3.api_version;
    pointer3.kind = gen3.kind;
    pointer3.source_type = "taperql-apply";

    ExpectedActivePolicy stale_expected;
    stale_expected.generation = 1;
    stale_expected.policy_id = base.policy_id;
    const PromoteActiveResult promote_res =
        store_->compare_and_promote_active(key_, stale_expected, pointer3);
    EXPECT_FALSE(promote_res.ok);
    EXPECT_EQ(promote_res.code, PolicyStateErrorCode::ActivePointerConflict);
}

TEST_F(PolicyApplyTransactionTest, CompileFailureFailsBeforeStoreVersion) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    PolicyUpdateJob job = make_apply_job(key_, base, "%%% invalid yaml %%%", "job-bad-yaml");
    PolicyApplyTransaction transaction(tx_config_);
    const PolicyApplyTransactionResult result = transaction.execute(job);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, "POLICY_APPLY_COMPILE_FAILED");
    EXPECT_EQ(result.failure_stage, PolicyApplyStage::BuildCandidate);

    const auto version = store_->load_policy_version(key_, 2);
    EXPECT_FALSE(version.ok);
}

TEST_F(PolicyApplyTransactionTest, LoadActiveFailureFails) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(base);
    ASSERT_TRUE(emit.ok);

    PolicyUpdateJob job = make_apply_job(key_, base, emit.yaml, "job-no-active");
    PolicyApplyTransaction transaction(tx_config_);
    const PolicyApplyTransactionResult result = transaction.execute(job);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, "POLICY_APPLY_LOAD_ACTIVE_FAILED");
}

TEST_F(PolicyApplyTransactionTest, StoreVersionConflictFails) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    TqPolicyDocument conflicting = make_policy_doc("route-conflict", 2, base.policy_id);
    PolicyIrYamlEmitResult conflict_emit = emit_policy_ir_canonical_yaml(conflicting);
    ASSERT_TRUE(conflict_emit.ok);

    PolicyVersionRecord version2;
    version2.generation = 2;
    version2.policy_id = conflicting.policy_id;
    version2.schema_version = 1;
    version2.api_version = conflicting.api_version;
    version2.kind = conflicting.kind;
    version2.source_type = "taperql-apply";
    version2.canonical_hash = canonical_hash_for_yaml(conflict_emit.yaml);
    ASSERT_TRUE(store_->store_policy_version(key_, version2, conflict_emit.yaml).ok);

    PolicyUpdateJob job = make_apply_job(key_, base, candidate_emit.yaml, "job-store-conflict");
    PolicyApplyTransaction transaction(tx_config_);
    const PolicyApplyTransactionResult result = transaction.execute(job);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, "POLICY_APPLY_STORE_VERSION_FAILED");
    EXPECT_EQ(result.failure_stage, PolicyApplyStage::StoreVersion);
}

TEST_F(PolicyApplyTransactionTest, IdempotentVersionStoreSucceeds) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    PolicyUpdateJob job = make_apply_job(key_, base, candidate_emit.yaml, "job-idempotent");
    PolicyApplyTransaction transaction(tx_config_);
    const PolicyApplyTransactionResult first = transaction.execute(job);
    ASSERT_TRUE(first.ok) << first.error_message;
    EXPECT_EQ(first.final_state, PolicyUpdateJobState::Committed);

    const auto stored_version = store_->load_policy_version(key_, first.candidate_generation);
    ASSERT_TRUE(stored_version.ok);

    PolicyVersionRecord repeat_record = stored_version.record;
    const StorePolicyVersionResult repeat_store =
        store_->store_policy_version(key_, repeat_record, stored_version.canonical_yaml);
    ASSERT_TRUE(repeat_store.ok) << repeat_store.error;

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, first.candidate_generation);
    EXPECT_EQ(active.pointer.policy_id, first.candidate_policy_id);
}

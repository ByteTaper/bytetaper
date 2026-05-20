// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_config.h"
#include "control_plane/control_plane_service.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_generation_commit.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "control_plane/runtime_status_report.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <random>
#include <vector>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_manual_res_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation) {
    TqPolicyDocument doc;
    doc.document_id = "manual-res-test";
    doc.source_name = "test";
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

void seed_generation(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
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
}

void promote_generation(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                        const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    ActivePolicyPointer pointer;
    pointer.generation = generation;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    pointer.version_key = make_version_key(key, generation);
    pointer.yaml_key = make_yaml_key(key, generation);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    const auto active_res = store.load_active_pointer(key);
    if (active_res.ok) {
        expected.generation = active_res.pointer.generation;
        expected.policy_id = active_res.pointer.policy_id;
    } else {
        expected.generation = 0;
    }
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

void seed_active_policy(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                        const TqPolicyDocument& doc, std::uint64_t generation) {
    seed_generation(store, key, doc, generation);
    promote_generation(store, key, doc, generation);
}

class AuditCapturingPolicyStateStore : public PolicyStateStore {
public:
    explicit AuditCapturingPolicyStateStore(RocksDBPolicyStateStore* inner) : inner_(inner) {}

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
        return inner_->compare_and_promote_active(key, expected, next);
    }

    AppendAuditResult append_audit_record(const PolicyResourceKey& key,
                                          const PolicyAuditRecord& record) override {
        captured_.push_back(record);
        return inner_->append_audit_record(key, record);
    }

    CommitPolicyGenerationWithAuditResult commit_policy_generation_with_audit(
        const CommitPolicyGenerationWithAuditParams& params) override {
        captured_.push_back(params.audit);
        return inner_->commit_policy_generation_with_audit(params);
    }

    StorePolicyUpdateJobResult store_policy_update_job(const PolicyResourceKey& key,
                                                       const PolicyUpdateJobRecord& job) override {
        return inner_->store_policy_update_job(key, job);
    }

    LoadPolicyUpdateJobResult load_policy_update_job(const PolicyResourceKey& key,
                                                     const std::string& job_id) override {
        return inner_->load_policy_update_job(key, job_id);
    }

    std::optional<PolicyAuditRecord> find_audit(const std::string& request_id) const {
        for (const PolicyAuditRecord& record : captured_) {
            if (record.request_id == request_id) {
                return record;
            }
        }
        return std::nullopt;
    }

    std::optional<PolicyAuditRecord> find_last_audit(const std::string& request_id) const {
        std::optional<PolicyAuditRecord> last;
        for (const PolicyAuditRecord& record : captured_) {
            if (record.request_id == request_id) {
                last = record;
            }
        }
        return last;
    }

    std::size_t audit_count() const {
        return captured_.size();
    }

private:
    RocksDBPolicyStateStore* inner_;
    std::vector<PolicyAuditRecord> captured_;
};

class AuditFailingPolicyStateStore : public PolicyStateStore {
public:
    explicit AuditFailingPolicyStateStore(RocksDBPolicyStateStore* inner) : inner_(inner) {}

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
        return inner_->compare_and_promote_active(key, expected, next);
    }

    AppendAuditResult append_audit_record(const PolicyResourceKey&,
                                          const PolicyAuditRecord&) override {
        AppendAuditResult result{};
        result.ok = false;
        result.code = PolicyStateErrorCode::AuditWriteFailed;
        result.error = "audit write rejected";
        return result;
    }

    CommitPolicyGenerationWithAuditResult
    commit_policy_generation_with_audit(const CommitPolicyGenerationWithAuditParams&) override {
        CommitPolicyGenerationWithAuditResult result{};
        result.ok = false;
        result.code = PolicyStateErrorCode::AuditWriteFailed;
        result.error = "audit write rejected";
        return result;
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
    RocksDBPolicyStateStore* inner_;
};

AdoptLocalFetchedPolicy make_adopt_fetch_result(const std::string& yaml,
                                                std::uint64_t local_generation,
                                                const std::string& policy_id,
                                                const std::string& canonical_hash) {
    AdoptLocalFetchedPolicy fetched{};
    fetched.ok = true;
    fetched.canonical_yaml = yaml;
    fetched.local_generation = local_generation;
    fetched.local_policy_id = policy_id;
    fetched.local_canonical_hash = canonical_hash;
    return fetched;
}

RuntimeStatusReport make_runtime_report(const std::string& runtime_id) {
    RuntimeStatusReport report{};
    report.runtime_id = runtime_id;
    report.resource_key = PolicyResourceKey::default_runtime().to_string();
    report.gateway_adapter = "envoy";
    report.active_generation = 1;
    report.active_policy_id = "policy-id";
    report.active_canonical_hash = "sha256:abc";
    report.activation_status = "active";
    report.data_path_mode = "normal";
    report.control_plane_reachable = true;
    report.last_pull_at_unix_epoch_ms = 1'000'000;
    report.received_at_unix_epoch_ms = 1'000'000;
    return report;
}

class ManualResolutionServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db(db_path_);
        store_.emplace(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        key_ = PolicyResourceKey::default_runtime();

        config_.policy_state_store = &*store_;
        repair_hook_calls_ = 0;
        config_.repair_local_hook = [this](const PolicyRepairLocalRequest&,
                                           const ActivePolicyPointer&) {
            repair_hook_calls_++;
            RepairLocalHookResult hook{};
            hook.ok = true;
            return hook;
        };

        service_ = std::make_unique<ControlPlaneService>(config_);
    }

    void TearDown() override {
        service_.reset();
        destroy_db(db_path_);
    }

    void register_runtime(const std::string& runtime_id) {
        RuntimeStatusReport report = make_runtime_report(runtime_id);
        ASSERT_TRUE(service_->report_runtime_status(report).ok);
    }

    std::string db_path_;
    std::optional<RocksDBPolicyStateStore> store_;
    PolicyResourceKey key_;
    ControlPlaneServiceConfig config_;
    std::unique_ptr<ControlPlaneService> service_;
    int repair_hook_calls_ = 0;
};

TEST_F(ManualResolutionServiceTest, RollbackWithoutConfirmRejected) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    request.expected_current_generation = 1;
    request.expected_current_policy_id = gen1.policy_id;
    request.request_id = "rollback-no-confirm";
    request.confirm = false;

    const PolicyRollbackResult result = service_->rollback(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrRollbackConfirmationRequired);
}

TEST_F(ManualResolutionServiceTest, RollbackTargetMissingRejected) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 99;
    request.expected_current_generation = 1;
    request.expected_current_policy_id = gen1.policy_id;
    request.request_id = "rollback-missing-target";
    request.confirm = true;

    const PolicyRollbackResult result = service_->rollback(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrRollbackTargetNotFound);
}

TEST_F(ManualResolutionServiceTest, RollbackExpectedCurrentMismatchRejected) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    request.expected_current_generation = 2;
    request.expected_current_policy_id = gen1.policy_id;
    request.request_id = "rollback-mismatch";
    request.confirm = true;

    const PolicyRollbackResult result = service_->rollback(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrRollbackExpectedCurrentMismatch);
}

TEST_F(ManualResolutionServiceTest, RollbackCreatesNewGenerationFromOldContent) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument gen2 = make_policy_doc("route-b", 2);
    const TqPolicyDocument gen3 = make_policy_doc("route-c", 3);
    seed_active_policy(*store_, key_, gen1, 1);
    seed_generation(*store_, key_, gen2, 2);
    promote_generation(*store_, key_, gen2, 2);
    seed_generation(*store_, key_, gen3, 3);
    promote_generation(*store_, key_, gen3, 3);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    request.expected_current_generation = 3;
    request.expected_current_policy_id = gen3.policy_id;
    request.request_id = "rollback-success";
    request.confirm = true;

    const PolicyRollbackResult result = service_->rollback(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::RollbackAccepted);
    EXPECT_EQ(result.new_generation, 4u);
    EXPECT_EQ(result.from_generation, 3u);
    EXPECT_EQ(result.target_generation, 1u);

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 4u);
    EXPECT_EQ(active.pointer.source_type, "rollback");

    const auto gen1_version = store_->load_policy_version(key_, 1);
    const auto gen4_version = store_->load_policy_version(key_, 4);
    ASSERT_TRUE(gen1_version.ok);
    ASSERT_TRUE(gen4_version.ok);
    EXPECT_EQ(gen4_version.record.policy_id, gen1_version.record.policy_id);
    EXPECT_EQ(gen4_version.record.canonical_hash, gen1_version.record.canonical_hash);
    EXPECT_EQ(gen4_version.canonical_yaml, gen1_version.canonical_yaml);

    const auto gen2_still = store_->load_policy_version(key_, 2);
    const auto gen3_still = store_->load_policy_version(key_, 3);
    EXPECT_TRUE(gen2_still.ok);
    EXPECT_TRUE(gen3_still.ok);
}

TEST_F(ManualResolutionServiceTest, AdoptLocalWithoutConfirmationRejected) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument divergent = make_policy_doc("route-divergent", 2);
    seed_active_policy(*store_, key_, gen1, 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(divergent);
    ASSERT_TRUE(emit.ok);
    config_.adopt_local_fetch_hook = [&](const PolicyAdoptLocalRequest&,
                                         const RuntimeStatusReport&) {
        return make_adopt_fetch_result(emit.yaml, 2, divergent.policy_id,
                                       canonical_hash_for_yaml(emit.yaml));
    };
    service_ = std::make_unique<ControlPlaneService>(config_);
    register_runtime("runtime-adopt");

    PolicyAdoptLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "runtime-adopt";
    request.expected_current_generation = 1;
    request.request_id = "adopt-no-confirm";
    request.confirm_divergent_adoption = false;

    const PolicyAdoptLocalResult result = service_->adopt_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrAdoptLocalConfirmationRequired);
}

TEST_F(ManualResolutionServiceTest, AdoptLocalRequiresRuntimeId) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyAdoptLocalRequest request{};
    request.resource_key = key_;
    request.expected_current_generation = 1;
    request.request_id = "adopt-no-runtime";
    request.confirm_divergent_adoption = true;

    const PolicyAdoptLocalResult result = service_->adopt_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedInvalidRequest);
    EXPECT_EQ(result.error_code, kErrManualInvalidRequest);
    EXPECT_EQ(result.error, "runtimeId is required");
}

TEST_F(ManualResolutionServiceTest, AdoptLocalUnknownRuntimeRejected) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);
    config_.adopt_local_fetch_hook = [](const PolicyAdoptLocalRequest&,
                                        const RuntimeStatusReport&) {
        AdoptLocalFetchedPolicy fetched{};
        fetched.ok = true;
        fetched.canonical_yaml = "apiVersion: bytetaper.io/v1alpha1\nkind: RuntimePolicy\n";
        return fetched;
    };
    service_ = std::make_unique<ControlPlaneService>(config_);

    PolicyAdoptLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "missing-runtime";
    request.expected_current_generation = 1;
    request.request_id = "adopt-missing-runtime";
    request.confirm_divergent_adoption = true;

    const PolicyAdoptLocalResult result = service_->adopt_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrAdoptLocalPolicyUnavailable);
}

TEST_F(ManualResolutionServiceTest, AdoptLocalInvalidYamlRejected) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);
    config_.adopt_local_fetch_hook = [](const PolicyAdoptLocalRequest&,
                                        const RuntimeStatusReport&) {
        return make_adopt_fetch_result("not: [valid", 2, "policy-id", "sha256:abc");
    };
    service_ = std::make_unique<ControlPlaneService>(config_);
    register_runtime("runtime-adopt");

    PolicyAdoptLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "runtime-adopt";
    request.expected_current_generation = 1;
    request.request_id = "adopt-invalid";
    request.confirm_divergent_adoption = true;

    const PolicyAdoptLocalResult result = service_->adopt_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrAdoptLocalPolicyInvalid);
}

TEST_F(ManualResolutionServiceTest, AdoptLocalStoresNewCommittedGeneration) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument divergent = make_policy_doc("route-divergent", 2);
    seed_active_policy(*store_, key_, gen1, 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(divergent);
    ASSERT_TRUE(emit.ok);
    config_.adopt_local_fetch_hook = [&](const PolicyAdoptLocalRequest&,
                                         const RuntimeStatusReport&) {
        return make_adopt_fetch_result(emit.yaml, 2, divergent.policy_id,
                                       canonical_hash_for_yaml(emit.yaml));
    };
    service_ = std::make_unique<ControlPlaneService>(config_);
    register_runtime("runtime-adopt");

    PolicyAdoptLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "runtime-adopt";
    request.expected_current_generation = 1;
    request.local_generation = 2;
    request.local_policy_id = divergent.policy_id;
    request.request_id = "adopt-success";
    request.confirm_divergent_adoption = true;

    const PolicyAdoptLocalResult result = service_->adopt_local(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::AdoptLocalAccepted);
    EXPECT_EQ(result.new_generation, 2u);
    EXPECT_EQ(result.new_policy_id, divergent.policy_id);

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 2u);
    EXPECT_EQ(active.pointer.source_type, "manual-adopt");
}

TEST_F(ManualResolutionServiceTest, RepairLocalWithoutConfirmRejected) {
    register_runtime("runtime-a");
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRepairLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "runtime-a";
    request.expected_committed_generation = 1;
    request.expected_committed_policy_id = gen1.policy_id;
    request.request_id = "repair-no-confirm";
    request.confirm = false;

    const PolicyRepairLocalResult result = service_->repair_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrManualConfirmationRequired);
}

TEST_F(ManualResolutionServiceTest, RepairLocalCommittedMismatchRejected) {
    register_runtime("runtime-a");
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRepairLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "runtime-a";
    request.expected_committed_generation = 2;
    request.expected_committed_policy_id = gen1.policy_id;
    request.request_id = "repair-mismatch";
    request.confirm = true;

    const PolicyRepairLocalResult result = service_->repair_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrRepairCommittedVersionMismatch);
}

TEST_F(ManualResolutionServiceTest, RepairLocalDoesNotChangeActivePointer) {
    register_runtime("runtime-a");
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    const auto before = store_->load_active_pointer(key_);
    ASSERT_TRUE(before.ok);

    PolicyRepairLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "runtime-a";
    request.expected_committed_generation = 1;
    request.expected_committed_policy_id = gen1.policy_id;
    request.request_id = "repair-success";
    request.confirm = true;

    const PolicyRepairLocalResult result = service_->repair_local(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::RepairLocalAccepted);
    EXPECT_EQ(repair_hook_calls_, 1);

    const auto after = store_->load_active_pointer(key_);
    ASSERT_TRUE(after.ok);
    EXPECT_EQ(after.pointer.generation, before.pointer.generation);
    EXPECT_EQ(after.pointer.policy_id, before.pointer.policy_id);
}

TEST_F(ManualResolutionServiceTest, RepairLocalUnknownRuntimeRejected) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRepairLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "missing-runtime";
    request.expected_committed_generation = 1;
    request.expected_committed_policy_id = gen1.policy_id;
    request.request_id = "repair-missing-runtime";
    request.confirm = true;

    const PolicyRepairLocalResult result = service_->repair_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrRepairTargetRuntimeNotFound);
}

TEST_F(ManualResolutionServiceTest, GenerationCommitVersionConflictFailsSafely) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument divergent_a = make_policy_doc("route-divergent-a", 2);
    const TqPolicyDocument divergent_b = make_policy_doc("route-divergent-b", 2);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyIrYamlEmitResult emit_a = emit_policy_ir_canonical_yaml(divergent_a);
    PolicyIrYamlEmitResult emit_b = emit_policy_ir_canonical_yaml(divergent_b);
    ASSERT_TRUE(emit_a.ok);
    ASSERT_TRUE(emit_b.ok);

    config_.adopt_local_fetch_hook = [&](const PolicyAdoptLocalRequest&,
                                         const RuntimeStatusReport&) {
        return make_adopt_fetch_result(emit_a.yaml, 2, divergent_a.policy_id,
                                       canonical_hash_for_yaml(emit_a.yaml));
    };
    service_ = std::make_unique<ControlPlaneService>(config_);
    register_runtime("runtime-adopt-conflict");

    PolicyAdoptLocalRequest adopt{};
    adopt.resource_key = key_;
    adopt.runtime_id = "runtime-adopt-conflict";
    adopt.expected_current_generation = 1;
    adopt.local_generation = 2;
    adopt.request_id = "adopt-conflict-1";
    adopt.confirm_divergent_adoption = true;
    ASSERT_TRUE(service_->adopt_local(adopt).ok);

    PolicyGenerationCommitInput conflict{};
    conflict.resource_key = key_;
    conflict.expected_active.generation = 2;
    conflict.expected_active.policy_id = divergent_a.policy_id;
    conflict.canonical_yaml = emit_b.yaml;
    conflict.source_type = "manual-adopt";
    conflict.apply_id = "adopt-conflict-2";
    conflict.request_id = "adopt-conflict-2";
    conflict.candidate_generation_override = 2;

    const PolicyGenerationCommitResult result = commit_policy_generation(&*store_, conflict);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrManualOperationPromoteConflict);
}

TEST_F(ManualResolutionServiceTest, SuccessfulManualOperationsAppendAuditRecords) {
    auto audit_store = std::make_unique<AuditCapturingPolicyStateStore>(&*store_);
    config_.policy_state_store = audit_store.get();
    service_ = std::make_unique<ControlPlaneService>(config_);

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument gen2 = make_policy_doc("route-b", 2);
    const TqPolicyDocument gen3 = make_policy_doc("route-c", 3);
    const TqPolicyDocument divergent = make_policy_doc("route-divergent", 2);
    seed_active_policy(*store_, key_, gen1, 1);
    seed_generation(*store_, key_, gen2, 2);
    promote_generation(*store_, key_, gen2, 2);
    seed_generation(*store_, key_, gen3, 3);
    promote_generation(*store_, key_, gen3, 3);

    PolicyIrYamlEmitResult emit_div = emit_policy_ir_canonical_yaml(divergent);
    ASSERT_TRUE(emit_div.ok);

    config_.adopt_local_fetch_hook = [&](const PolicyAdoptLocalRequest&,
                                         const RuntimeStatusReport&) {
        return make_adopt_fetch_result(emit_div.yaml, 2, divergent.policy_id,
                                       canonical_hash_for_yaml(emit_div.yaml));
    };
    service_ = std::make_unique<ControlPlaneService>(config_);
    register_runtime("runtime-a");

    PolicyRollbackRequest rollback{};
    rollback.resource_key = key_;
    rollback.target_generation = 1;
    rollback.expected_current_generation = 3;
    rollback.expected_current_policy_id = gen3.policy_id;
    rollback.operator_id = "operator-rollback";
    rollback.request_id = "audit-rollback-success";
    rollback.confirm = true;
    const PolicyRollbackResult rollback_result = service_->rollback(rollback);
    ASSERT_TRUE(rollback_result.ok) << rollback_result.error;

    const auto active_after_rollback = store_->load_active_pointer(key_);
    ASSERT_TRUE(active_after_rollback.ok);
    ASSERT_EQ(active_after_rollback.pointer.generation, rollback_result.new_generation);

    PolicyAdoptLocalRequest adopt{};
    adopt.resource_key = key_;
    adopt.runtime_id = "runtime-a";
    adopt.expected_current_generation = active_after_rollback.pointer.generation;
    adopt.local_generation = active_after_rollback.pointer.generation + 1;
    adopt.local_policy_id = divergent.policy_id;
    adopt.operator_id = "operator-adopt";
    adopt.request_id = "audit-adopt-success";
    adopt.confirm_divergent_adoption = true;
    const PolicyAdoptLocalResult adopt_result = service_->adopt_local(adopt);
    ASSERT_TRUE(adopt_result.ok) << adopt_result.error;

    const auto active_after_adopt = store_->load_active_pointer(key_);
    ASSERT_TRUE(active_after_adopt.ok);

    PolicyRepairLocalRequest repair{};
    repair.resource_key = key_;
    repair.runtime_id = "runtime-a";
    repair.expected_committed_generation = active_after_adopt.pointer.generation;
    repair.expected_committed_policy_id = active_after_adopt.pointer.policy_id;
    repair.operator_id = "operator-repair";
    repair.request_id = "audit-repair-success";
    repair.confirm = true;
    const PolicyRepairLocalResult repair_result = service_->repair_local(repair);
    ASSERT_TRUE(repair_result.ok) << repair_result.error;

    EXPECT_EQ(audit_store->audit_count(), 4u);

    const std::optional<PolicyAuditRecord> rollback_audit =
        audit_store->find_audit(rollback.request_id);
    ASSERT_TRUE(rollback_audit.has_value());
    EXPECT_EQ(rollback_audit->operation, "rollback");
    EXPECT_EQ(rollback_audit->source_type, "rollback");
    EXPECT_EQ(rollback_audit->operator_id, "operator-rollback");
    EXPECT_EQ(rollback_audit->request_id, rollback.request_id);
    EXPECT_EQ(rollback_audit->result, "success");
    EXPECT_EQ(rollback_audit->target_generation, 1u);
    EXPECT_EQ(rollback_audit->after_generation, rollback_result.new_generation);
    EXPECT_GE(rollback_audit->recorded_at_unix_epoch_ms, 0u);

    const std::optional<PolicyAuditRecord> adopt_audit = audit_store->find_audit(adopt.request_id);
    ASSERT_TRUE(adopt_audit.has_value());
    EXPECT_EQ(adopt_audit->operation, "adopt-local");
    EXPECT_EQ(adopt_audit->source_type, "manual-adopt");
    EXPECT_EQ(adopt_audit->operator_id, "operator-adopt");
    EXPECT_EQ(adopt_audit->request_id, adopt.request_id);
    EXPECT_EQ(adopt_audit->result, "success");
    EXPECT_EQ(adopt_audit->generation, adopt_result.new_generation);
    EXPECT_EQ(adopt_audit->policy_id, adopt_result.new_policy_id);
    EXPECT_EQ(adopt_audit->target_generation, 2u);
    EXPECT_EQ(adopt_result.resolved_local_generation, 2u);

    const std::optional<PolicyAuditRecord> repair_audit =
        audit_store->find_last_audit(repair.request_id);
    ASSERT_TRUE(repair_audit.has_value());
    EXPECT_EQ(repair_audit->operation, "repair-local");
    EXPECT_EQ(repair_audit->source_type, "manual-repair");
    EXPECT_EQ(repair_audit->operator_id, "operator-repair");
    EXPECT_EQ(repair_audit->request_id, repair.request_id);
    EXPECT_EQ(repair_audit->result, "success");
    EXPECT_TRUE(repair_audit->failure_reason.empty());
    EXPECT_EQ(repair_audit->before_generation, active_after_adopt.pointer.generation);
    EXPECT_EQ(repair_audit->after_generation, active_after_adopt.pointer.generation);
}

TEST_F(ManualResolutionServiceTest, AdoptLocalAuditTargetGenerationFromFetchedPolicy) {
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument divergent = make_policy_doc("route-divergent", 2);
    seed_active_policy(*store_, key_, gen1, 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(divergent);
    ASSERT_TRUE(emit.ok);
    constexpr std::uint64_t kFetchedLocalGeneration = 7;
    config_.adopt_local_fetch_hook = [&](const PolicyAdoptLocalRequest&,
                                         const RuntimeStatusReport&) {
        return make_adopt_fetch_result(emit.yaml, kFetchedLocalGeneration, divergent.policy_id,
                                       canonical_hash_for_yaml(emit.yaml));
    };
    service_ = std::make_unique<ControlPlaneService>(config_);
    register_runtime("runtime-adopt");

    PolicyAdoptLocalRequest request{};
    request.resource_key = key_;
    request.runtime_id = "runtime-adopt";
    request.expected_current_generation = 1;
    request.local_policy_id = divergent.policy_id;
    request.request_id = "adopt-audit-target-gen";
    request.confirm_divergent_adoption = true;

    const PolicyAdoptLocalResult result = service_->adopt_local(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.resolved_local_generation, kFetchedLocalGeneration);

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, kFetchedLocalGeneration);
}

TEST_F(ManualResolutionServiceTest, ManualOperationFailsWhenAuditPersistenceFails) {
    auto audit_failing_store = std::make_unique<AuditFailingPolicyStateStore>(&*store_);
    config_.policy_state_store = audit_failing_store.get();
    service_ = std::make_unique<ControlPlaneService>(config_);

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument gen2 = make_policy_doc("route-b", 2);
    seed_active_policy(*store_, key_, gen1, 1);
    seed_generation(*store_, key_, gen2, 2);
    promote_generation(*store_, key_, gen2, 2);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    request.expected_current_generation = 2;
    request.expected_current_policy_id = gen2.policy_id;
    request.request_id = "audit-write-failure";
    request.confirm = true;

    const PolicyRollbackResult result = service_->rollback(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrManualAuditWriteFailed);
    EXPECT_NE(result.error.find("audit record could not be persisted"), std::string::npos);

    const auto active = store_->load_active_pointer(key_);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 2u);
}

TEST_F(ManualResolutionServiceTest, FailedManualOperationStillAppendsAuditRecord) {
    auto audit_store = std::make_unique<AuditCapturingPolicyStateStore>(&*store_);
    config_.policy_state_store = audit_store.get();
    service_ = std::make_unique<ControlPlaneService>(config_);

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    request.expected_current_generation = 1;
    request.expected_current_policy_id = gen1.policy_id;
    request.operator_id = "operator-fail";
    request.request_id = "audit-rollback-failure";
    request.confirm = false;

    const PolicyRollbackResult result = service_->rollback(request);
    EXPECT_FALSE(result.ok);

    EXPECT_EQ(audit_store->audit_count(), 1u);

    const std::optional<PolicyAuditRecord> audit = audit_store->find_audit(request.request_id);
    ASSERT_TRUE(audit.has_value());
    EXPECT_EQ(audit->operation, "rollback");
    EXPECT_EQ(audit->result, "failure");
    EXPECT_FALSE(audit->failure_reason.empty());
}

} // namespace

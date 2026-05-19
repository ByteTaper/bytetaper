// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_config.h"
#include "control_plane/control_plane_service.h"
#include "control_plane/policy_apply_api.h"
#include "control_plane/policy_apply_status.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <random>
#include <rocksdb/db.h>

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
        service_ = std::make_unique<ControlPlaneService>(config_);
    }

    void TearDown() override {
        service_.reset();
        store_.reset();
        destroy_db(db_path_);
    }

    std::string db_path_;
    PolicyResourceKey key_;
    ControlPlaneServiceConfig config_;
    std::optional<RocksDBPolicyStateStore> store_;
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

TEST_F(ControlPlaneServiceContractTest, ApplyAcceptedWithValidCas) {
    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(*store_, key_, base, 1);

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
    EXPECT_STREQ(to_string(PolicyApplyStatus::Accepted), "Accepted");
    EXPECT_STREQ(to_string(PolicyApplyStatus::RollbackPlanReady), "RollbackPlanReady");
}

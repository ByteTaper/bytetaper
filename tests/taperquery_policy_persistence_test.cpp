// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace bytetaper::taperquery;
namespace fs = std::filesystem;

namespace {

TqPolicyDocument create_sample_policy() {
    TqPolicyDocument doc;
    doc.document_id = "persistence-sample";
    doc.source_name = "test-suite";
    doc.expected_base_sha = "aabbccddeeff00112233445566778899aabbccdd";
    doc.version.source_schema_version = "v1";
    doc.version.policy_ir_version = "tq-ir/v1";
    doc.version.identity_version = "policy-identity/v2";

    TqRoutePolicy r;
    r.route_id = "test-route-1";
    r.match_prefix = "/api/v1";
    r.match_kind = TqRouteMatchKind::Prefix;
    r.mutation = TqMutationMode::Disabled;
    r.allowed_method = TqHttpMethod::Get;
    r.max_response_bytes = 10000;
    doc.routes.push_back(r);

    return doc;
}

} // namespace

TEST(TaperQueryPolicyPersistenceTest, AtomicWriteAndRecoveryFlow) {
    fs::path test_dir = fs::current_path() / "test_persistence_state";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    std::string policy_identity = compute_policy_document_identity(doc);
    uint64_t gen = 42;

    // 1. Initial State: No files exist, recovery should fail gracefully
    auto initial_recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(initial_recovery.ok);

    // 2. Apply and persist
    PersistedPolicyMetadata metadata;
    metadata.policy_identity = policy_identity;
    metadata.generation = gen;
    metadata.source_type = "admin-api";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.operator_id = "test-operator";
    metadata.request_id = "req-123";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok) << "Persist failed: " << persist_res.error;

    // Check files exist
    EXPECT_TRUE(fs::exists(test_dir / "active.yaml"));
    EXPECT_TRUE(fs::exists(test_dir / "active.meta.json"));

    // 3. Recover successfully
    auto recovery = load_persisted_active_policy(config);
    ASSERT_TRUE(recovery.ok) << "Recovery failed: " << recovery.error;
    EXPECT_EQ(recovery.metadata.generation, gen);
    EXPECT_EQ(recovery.metadata.policy_identity, policy_identity);
    EXPECT_EQ(recovery.document.document_id, "persistence-sample");
    ASSERT_EQ(recovery.document.routes.size(), 1u);
    EXPECT_EQ(recovery.document.routes[0].route_id, "test-route-1");

    // 4. Verify tampering detection (Corrupted YAML file)
    {
        std::ofstream tampered(test_dir / "active.yaml", std::ios::trunc);
        tampered << "this is tampered content that doesn't match the original sha256\n";
    }

    auto tampered_recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(tampered_recovery.ok);
    EXPECT_NE(tampered_recovery.error.find("integrity check failed"), std::string::npos);

    // Cleanup
    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, InvalidJsonMetadataRecovery) {
    fs::path test_dir = fs::current_path() / "test_persistence_state_invalid_json";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    // Write a dummy YAML
    {
        std::ofstream yaml_file(test_dir / "active.yaml");
        yaml_file << "routes: []\n";
    }

    // Write corrupted JSON
    {
        std::ofstream meta_file(test_dir / "active.meta.json");
        meta_file << "{invalid json content\n";
    }

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);

    fs::remove_all(test_dir);
}

#include "runtime/policy_snapshot.h"
#include "taperquery/tq_apply_service.h"

namespace runtime = bytetaper::runtime;

TEST(TaperQueryPolicyPersistenceTest, PersistenceFailurePreventsSwap) {
    fs::path test_dir = fs::current_path() / "test_persistence_failure";
    fs::remove_all(test_dir);

    // Initialize policy store
    runtime::RuntimePolicyStore store;
    TqPolicyDocument init_doc;
    init_doc.document_id = "initial";
    init_doc.version.source_schema_version = "tq/v1";
    init_doc.expected_base_sha = "";
    TqRoutePolicy r;
    r.route_id = "initial_route";
    r.match_prefix = "/initial";
    r.match_kind = TqRouteMatchKind::Prefix;
    r.mutation = TqMutationMode::Disabled;
    r.allowed_method = TqHttpMethod::Any;
    init_doc.routes.push_back(r);

    auto build_res =
        runtime::build_runtime_policy_snapshot_from_ir(init_doc, store.next_generation());
    std::string err;
    ASSERT_TRUE(store.install_initial(build_res.snapshot, &err));
    std::string initial_identity = build_res.snapshot->policy_identity;

    // Configure persistence to fail (invalid directory path that cannot be created/written to)
    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = "/nonexistent/dir/path/doesnt/exist/at/all";
    config.active_policy_filename = "active-policy.yaml";
    config.metadata_filename = "active-policy.meta.json";

    TqApplyService service(&store, nullptr, nullptr, config);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/api\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);

    // Verify apply rejected on persistence failure, and store not swapped
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.status, TqApplyStatus::RejectedPersistenceFailed);
    EXPECT_EQ(store.load()->policy_identity, initial_identity);
    EXPECT_EQ(store.load()->generation, 1u);
}

TEST(TaperQueryPolicyPersistenceTest, SuccessfulApplyPersistsThenSwaps) {
    fs::path test_dir = fs::current_path() / "test_persistence_success";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    // Initialize policy store
    runtime::RuntimePolicyStore store;
    TqPolicyDocument init_doc;
    init_doc.document_id = "initial";
    init_doc.version.source_schema_version = "tq/v1";
    init_doc.expected_base_sha = "";
    TqRoutePolicy r;
    r.route_id = "initial_route";
    r.match_prefix = "/initial";
    r.match_kind = TqRouteMatchKind::Prefix;
    r.mutation = TqMutationMode::Disabled;
    r.allowed_method = TqHttpMethod::Any;
    init_doc.routes.push_back(r);

    auto build_res =
        runtime::build_runtime_policy_snapshot_from_ir(init_doc, store.next_generation());
    std::string err;
    ASSERT_TRUE(store.install_initial(build_res.snapshot, &err));
    std::string initial_identity = build_res.snapshot->policy_identity;

    // Configure valid persistence
    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active-policy.yaml";
    config.metadata_filename = "active-policy.meta.json";

    TqApplyService service(&store, nullptr, nullptr, config);

    TqApplyRequest req;
    req.source = "policy \"my-policy\" { route \"r1\" when path prefix \"/api\" {} }";
    req.expected_base_identity = initial_identity;
    req.mode = TqApplyMode::Apply;

    auto res = service.execute(req);

    // Verify swap and write succeeded
    ASSERT_TRUE(res.ok) << res.message;
    EXPECT_EQ(res.status, TqApplyStatus::Applied);
    EXPECT_EQ(store.load()->policy_identity, res.applied_policy_identity);
    EXPECT_EQ(store.load()->generation, 2u);

    // Verify files were actually written to disk
    EXPECT_TRUE(fs::exists(test_dir / "active-policy.yaml"));
    EXPECT_TRUE(fs::exists(test_dir / "active-policy.meta.json"));

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, RestartPrefersPersistedPolicy) {
    fs::path test_dir = fs::current_path() / "test_persistence_restart";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    std::string policy_identity = compute_policy_document_identity(doc);
    uint64_t gen = 99;

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = policy_identity;
    metadata.generation = gen;
    metadata.source_type = "admin-api";
    metadata.written_at_unix_epoch_ms = 999999;
    metadata.operator_id = "test-operator";
    metadata.request_id = "req-99";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    // Recovery preferred
    auto recovery = load_persisted_active_policy(config);
    ASSERT_TRUE(recovery.ok);
    EXPECT_EQ(recovery.metadata.generation, 99u);
    EXPECT_EQ(recovery.metadata.policy_identity, policy_identity);
    EXPECT_FALSE(recovery.files_missing);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, CorruptPersistedPolicyFailsStartup) {
    fs::path test_dir = fs::current_path() / "test_persistence_corrupt";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    std::string policy_identity = compute_policy_document_identity(doc);

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = policy_identity;
    metadata.generation = 12;
    metadata.source_type = "admin-api";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    // Corrupt yaml file
    {
        std::ofstream tampered(test_dir / "active.yaml", std::ios::trunc);
        tampered << "corrupted yaml content\n";
    }

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_FALSE(recovery.files_missing); // Hard corruption!

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, MissingPersistedPolicyFallsBackToBootstrap) {
    fs::path test_dir = fs::current_path() / "test_persistence_missing";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    // No files are created inside test_dir, calling recovery
    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_TRUE(recovery.files_missing); // Missing files - soft graceful recovery fallback!

    fs::remove_all(test_dir);
}

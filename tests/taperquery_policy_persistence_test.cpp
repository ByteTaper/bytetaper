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

TEST(TaperQueryPolicyPersistenceTest, ExtendedMetadataWritesRequiredFields) {
    fs::path test_dir = fs::current_path() / "test_persistence_extended";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    doc.generation = 42;
    doc.policy_id = compute_policy_document_identity(doc);
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.schema_version_num = 1;
    std::string policy_identity = doc.policy_id;

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = policy_identity;
    metadata.generation = 42;
    metadata.source_type = "taperql-apply";
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.apply_id = "req-123";
    metadata.previous_generation = 41;
    metadata.previous_policy_id = "sha256:prev1111";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "active.yaml";
    metadata.canonical_hash_algorithm = "sha256";
    metadata.bootstrap.file = "bootstrap.yaml";
    metadata.bootstrap.role = "initial-default-only";
    metadata.bootstrap.overwrite_protection = true;
    metadata.compatibility.policy_ir_version = "tq-ir/v1";
    metadata.written_at_unix_epoch_ms = 123456789;

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok) << "Persist failed: " << persist_res.error;

    // Verify fields in JSON directly
    std::ifstream f(test_dir / "active.meta.json");
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    EXPECT_NE(content.find("\"metadataSchemaVersion\": 1"), std::string::npos);
    EXPECT_NE(content.find("\"resourceKey\": \"policy/default/runtime\""), std::string::npos);
    EXPECT_NE(content.find("\"canonicalHash\": \"sha256:"), std::string::npos);

    // Verify recovery load
    auto recovery = load_persisted_active_policy(config);
    ASSERT_TRUE(recovery.ok) << "Recovery failed: " << recovery.error;
    EXPECT_EQ(recovery.metadata.metadata_schema_version, 1u);
    EXPECT_EQ(recovery.metadata.resource_key, "policy/default/runtime");
    EXPECT_EQ(recovery.metadata.apply_id, "req-123");
    EXPECT_EQ(recovery.metadata.previous_generation, 41u);
    EXPECT_EQ(recovery.metadata.previous_policy_id, "sha256:prev1111");
    EXPECT_EQ(recovery.metadata.schema_version, 1u);
    EXPECT_EQ(recovery.metadata.api_version, "bytetaper.io/v1alpha1");
    EXPECT_EQ(recovery.metadata.kind, "RuntimePolicy");
    EXPECT_EQ(recovery.metadata.active_policy_file, "active.yaml");
    EXPECT_EQ(recovery.metadata.canonical_hash_algorithm, "sha256");
    EXPECT_EQ(recovery.metadata.bootstrap.file, "bootstrap.yaml");
    EXPECT_EQ(recovery.metadata.bootstrap.role, "initial-default-only");
    EXPECT_TRUE(recovery.metadata.bootstrap.overwrite_protection);
    EXPECT_EQ(recovery.metadata.compatibility.policy_ir_version, "tq-ir/v1");

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, LegacyMetadataReadsMissingSchemaVersion) {
    fs::path test_dir = fs::current_path() / "test_persistence_legacy_read";
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
    metadata.generation = 5;

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    ASSERT_TRUE(recovery.ok) << "Recovery failed: " << recovery.error;
    EXPECT_EQ(recovery.metadata.metadata_schema_version, 0u);
    EXPECT_EQ(recovery.metadata.generation, 5u);
    EXPECT_TRUE(recovery.metadata.canonical_hash.empty());

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, UnsupportedSchemaVersionRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_unsupported_schema";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    PersistedPolicyMetadata metadata;
    metadata.policy_identity = compute_policy_document_identity(doc);

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    // Overwrite metadata JSON with unsupported schema version
    std::ifstream f_in(test_dir / "active.meta.json");
    std::string content((std::istreambuf_iterator<char>(f_in)), std::istreambuf_iterator<char>());
    f_in.close();

    std::size_t pos = content.find("\"metadataSchemaVersion\": 0");
    if (pos != std::string::npos) {
        content.replace(pos, 25, "\"metadataSchemaVersion\": 99");
    } else {
        content += "\n  \"metadataSchemaVersion\": 99\n";
    }

    {
        std::ofstream out(test_dir / "active.meta.json");
        out << content;
    }

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("version unsupported"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, CanonicalHashMismatchRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_hash_mismatch";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    PersistedPolicyMetadata metadata;
    metadata.policy_identity = compute_policy_document_identity(doc);
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.generation = 1;
    metadata.source_type = "taperql-apply";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "active.yaml";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    // Tamper YAML
    {
        std::ofstream out(test_dir / "active.yaml", std::ios::trunc);
        out << "routes: [] # corrupted\n";
    }

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("integrity check failed"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, AbsoluteActiveFilepathRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_absolute_path";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    PersistedPolicyMetadata metadata;
    metadata.policy_identity = compute_policy_document_identity(doc);
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.generation = 1;
    metadata.source_type = "taperql-apply";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    std::ifstream f_in(test_dir / "active.meta.json");
    std::string content((std::istreambuf_iterator<char>(f_in)), std::istreambuf_iterator<char>());
    f_in.close();

    std::size_t pos = content.find("\"activePolicyFile\": \"\"");
    if (pos != std::string::npos) {
        content.replace(pos, 22, "\"activePolicyFile\": \"/etc/evil\"");
    }

    {
        std::ofstream out(test_dir / "active.meta.json");
        out << content;
    }

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("path invalid"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, PathTraversalActiveFilepathRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_traversal_path";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    PersistedPolicyMetadata metadata;
    metadata.policy_identity = compute_policy_document_identity(doc);
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.generation = 1;
    metadata.source_type = "taperql-apply";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    std::ifstream f_in(test_dir / "active.meta.json");
    std::string content((std::istreambuf_iterator<char>(f_in)), std::istreambuf_iterator<char>());
    f_in.close();

    std::size_t pos = content.find("\"activePolicyFile\": \"\"");
    if (pos != std::string::npos) {
        content.replace(pos, 22, "\"activePolicyFile\": \"../evil\"");
    }

    {
        std::ofstream out(test_dir / "active.meta.json");
        out << content;
    }

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("path invalid"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, MissingResourceKeyTolerated) {
    fs::path test_dir = fs::current_path() / "test_persistence_missing_key_ok";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    PersistedPolicyMetadata metadata;
    metadata.policy_identity = compute_policy_document_identity(doc);
    metadata.metadata_schema_version = 0; // legacy schema

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    EXPECT_TRUE(recovery.ok);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, MissingResourceKeyRejectedForNewSchema) {
    fs::path test_dir = fs::current_path() / "test_persistence_missing_key_fail";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    PersistedPolicyMetadata metadata;
    metadata.policy_identity = compute_policy_document_identity(doc);
    metadata.metadata_schema_version = 1;
    metadata.resource_key = ""; // Empty!

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("missing required fields"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, GenerationMismatchRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_gen_mismatch";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.generation = 42;
    doc.policy_id = "sha256:somehash";
    doc.schema_version_num = 1;

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = "sha256:somehash";
    metadata.generation = 99; // Mismatch!
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "active.yaml";
    metadata.canonical_hash = "sha256:somehash";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";
    metadata.source_type = "taperql-apply";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("Generation mismatch"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, PolicyIdMismatchRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_policy_id_mismatch";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.generation = 42;
    doc.policy_id = "sha256:somehash";
    doc.schema_version_num = 1;

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = "sha256:mismatchedhash"; // Mismatch!
    metadata.generation = 42;
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "active.yaml";
    metadata.canonical_hash = "sha256:mismatchedhash";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";
    metadata.source_type = "taperql-apply";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("PolicyId mismatch"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, ApiVersionMismatchRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_api_version_mismatch";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.generation = 42;
    doc.policy_id = "sha256:somehash";
    doc.schema_version_num = 1;

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = "sha256:somehash";
    metadata.generation = 42;
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1beta1"; // Mismatch!
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "active.yaml";
    metadata.canonical_hash = "sha256:somehash";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";
    metadata.source_type = "taperql-apply";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("API version unsupported or mismatch"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, KindMismatchRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_kind_mismatch";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.generation = 42;
    doc.policy_id = "sha256:somehash";
    doc.schema_version_num = 1;

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = "sha256:somehash";
    metadata.generation = 42;
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "OtherKind"; // Mismatch!
    metadata.active_policy_file = "active.yaml";
    metadata.canonical_hash = "sha256:somehash";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";
    metadata.source_type = "taperql-apply";

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("Kind unsupported or mismatch"), std::string::npos);

    fs::remove_all(test_dir);
}

TEST(TaperQueryPolicyPersistenceTest, InvalidSourceTypeRejected) {
    fs::path test_dir = fs::current_path() / "test_persistence_source_type_invalid";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    LocalPolicyPersistenceConfig config;
    config.enabled = true;
    config.state_dir = test_dir.string();
    config.active_policy_filename = "active.yaml";
    config.metadata_filename = "active.meta.json";

    TqPolicyDocument doc = create_sample_policy();
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";
    doc.generation = 42;
    doc.policy_id = "sha256:somehash";
    doc.schema_version_num = 1;

    PersistedPolicyMetadata metadata;
    metadata.policy_identity = "sha256:somehash";
    metadata.generation = 42;
    metadata.metadata_schema_version = 1;
    metadata.resource_key = "policy/default/runtime";
    metadata.schema_version = 1;
    metadata.api_version = "bytetaper.io/v1alpha1";
    metadata.kind = "RuntimePolicy";
    metadata.active_policy_file = "active.yaml";
    metadata.canonical_hash = "sha256:somehash";
    metadata.written_at_unix_epoch_ms = 123456789;
    metadata.canonical_hash_algorithm = "sha256";
    metadata.source_type = "invalid-source"; // Invalid!

    auto persist_res = persist_active_policy_canonical_yaml(config, doc, metadata);
    ASSERT_TRUE(persist_res.ok);

    auto recovery = load_persisted_active_policy(config);
    EXPECT_FALSE(recovery.ok);
    EXPECT_NE(recovery.error.find("sourceType is invalid"), std::string::npos);

    fs::remove_all(test_dir);
}

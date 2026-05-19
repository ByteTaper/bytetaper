// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/policy_inactive_mode.h"
#include "runtime_policy/policy_mismatch_classifier.h"
#include "runtime_policy/runtime_policy_plane.h"
#include "runtime_policy/startup_policy_validator.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <rocksdb/db.h>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::runtime_policy;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_startup_val_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    rocksdb::DestroyDB(path, rocksdb::Options{});
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy(const std::string& route_id, std::uint64_t generation) {
    TqPolicyDocument doc;
    doc.document_id = "startup-val";
    doc.source_name = "test";
    doc.expected_base_sha = "0000000000000000000000000000000000000000";
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

void set_version_canonical_hash(PolicyVersionRecord& version, const std::string& yaml) {
    version.canonical_hash = canonical_hash_for_yaml(yaml);
}

PolicyMismatchInput make_match_input(std::uint64_t committed_gen, const std::string& committed_hash,
                                     std::uint64_t local_gen, const std::string& local_hash,
                                     bool local_exists = true, bool parseable = true) {
    PolicyMismatchInput input;
    input.committed_generation = committed_gen;
    input.committed_policy_id = "sha256:policy";
    input.committed_canonical_hash = committed_hash;
    input.local_file_exists = local_exists;
    input.local_file_parseable = parseable;
    input.local_generation = local_gen;
    input.local_policy_id = "sha256:policy";
    input.local_canonical_hash = local_hash;
    input.metadata_present = local_exists;
    input.metadata_valid = parseable;
    input.version_record_exists = true;
    input.version_hash_matches = true;
    input.committed_yaml_hash_matches = true;
    return input;
}

} // namespace

TEST(RuntimePolicyStartupValidationClassifier, ValidActivePointerMatchingLocalActivates) {
    PolicyMismatchInput input = make_match_input(2, "sha256:abc", 2, "sha256:abc");
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::Active);
}

TEST(RuntimePolicyStartupValidationClassifier, ActivePointerWithMissingLocalIsInactive) {
    PolicyMismatchInput input = make_match_input(2, "sha256:abc", 0, "", false, false);
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::InactiveMissingLocal);
}

TEST(RuntimePolicyStartupValidationClassifier, StoreAuthoritativeWithoutLocalMirrorActivates) {
    PolicyMismatchInput input = make_match_input(2, "sha256:abc", 0, "", false, false);
    input.require_local_mirror = false;
    input.committed_yaml_hash_matches = true;
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::Active);
}

TEST(RuntimePolicyStartupValidationClassifier, StaleLocalGenerationIsInactive) {
    PolicyMismatchInput input = make_match_input(3, "sha256:abc", 2, "sha256:abc");
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::InactiveStaleLocal);
}

TEST(RuntimePolicyStartupValidationClassifier, DivergedLocalGenerationIsInactive) {
    PolicyMismatchInput input = make_match_input(2, "sha256:abc", 5, "sha256:abc");
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::InactiveDivergedLocal);
}

TEST(RuntimePolicyStartupValidationClassifier, SameGenerationHashMismatchIsInactive) {
    PolicyMismatchInput input = make_match_input(2, "sha256:abc", 2, "sha256:def");
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::InactiveCorruptedLocal);
}

TEST(RuntimePolicyStartupValidationClassifier, UnparseableLocalYamlIsInactive) {
    PolicyMismatchInput input = make_match_input(2, "sha256:abc", 2, "sha256:abc");
    input.local_file_parseable = false;
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::InactiveUnparseableLocal);
}

TEST(RuntimePolicyStartupValidationClassifier, MissingVersionRecordIsInactive) {
    PolicyMismatchInput input = make_match_input(2, "sha256:abc", 2, "sha256:abc");
    input.version_hash_matches = false;
    EXPECT_EQ(classify_mismatch(input), RuntimePolicyHealth::InactiveActivePointerMissingTarget);
}

TEST(RuntimePolicyStartupValidationIntegration, CorruptLocalNotRestoredFromVersions) {
    const std::string db_path = make_temp_db_path();
    const std::string state_dir =
        (fs::current_path() / ("bt_startup_int_" + std::to_string(std::random_device{}())))
            .string();
    destroy_db(db_path);
    fs::remove_all(state_dir);
    fs::create_directories(state_dir);

    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    TqPolicyDocument doc = make_policy("route-a", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version;
    version.generation = 1;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    set_version_canonical_hash(version, emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    ActivePolicyPointer pointer;
    pointer.generation = 1;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = loaded.record.canonical_hash;
    pointer.version_key = make_version_key(key, 1);
    pointer.yaml_key = make_yaml_key(key, 1);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    LocalPolicyPersistenceConfig cfg;
    cfg.enabled = true;
    cfg.state_dir = state_dir;
    PersistedPolicyMetadata meta;
    meta.policy_identity = doc.policy_id;
    meta.generation = 1;
    meta.metadata_schema_version = 1;
    meta.resource_key = key.to_string();
    meta.schema_version = 1;
    meta.api_version = doc.api_version;
    meta.kind = doc.kind;
    meta.active_policy_file = "active-policy.yaml";
    meta.source_type = "taperql-apply";
    meta.written_at_unix_epoch_ms = 123;
    meta.canonical_hash_algorithm = "sha256";
    ASSERT_TRUE(persist_active_policy_canonical_yaml(cfg, doc, meta).ok);

    {
        const fs::path meta_path = fs::path(state_dir) / "active-policy.meta.json";
        std::ifstream meta_in(meta_path);
        std::string meta_json((std::istreambuf_iterator<char>(meta_in)),
                              std::istreambuf_iterator<char>());
        meta_in.close();
        const std::string needle = "\"canonicalHash\": \"sha256:";
        const std::size_t pos = meta_json.find(needle);
        ASSERT_NE(pos, std::string::npos);
        const std::size_t hash_start = pos + needle.size();
        const std::size_t hash_end = meta_json.find('"', hash_start);
        ASSERT_NE(hash_end, std::string::npos);
        meta_json.replace(hash_start, hash_end - hash_start,
                          "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        std::ofstream meta_out(meta_path, std::ios::trunc);
        meta_out << meta_json;
    }

    auto pointer_res = store.load_active_pointer(key);
    ASSERT_TRUE(pointer_res.ok);
    auto local = load_persisted_active_policy(cfg);

    StartupValidationInput input{};
    input.resource_key = &key;
    input.active_pointer = &pointer_res.pointer;
    input.local_load_result = &local;
    input.store = &store;
    input.validate_local_mirror = true;

    auto validation = validate_startup_against_active_pointer(input);
    EXPECT_FALSE(validation.valid);
    EXPECT_EQ(validation.health, RuntimePolicyHealth::InactiveCorruptedLocal);

    destroy_db(db_path);
    fs::remove_all(state_dir);
}

TEST(RuntimePolicyStartupValidationIntegration, StoreAuthoritativeWithoutLocalMirror) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    fs::remove_all(db_path);

    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    TqPolicyDocument doc = make_policy("route-store", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version;
    version.generation = 1;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    set_version_canonical_hash(version, emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    ActivePolicyPointer pointer;
    pointer.generation = 1;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = loaded.record.canonical_hash;
    pointer.version_key = make_version_key(key, 1);
    pointer.yaml_key = make_yaml_key(key, 1);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    StartupValidationInput input{};
    input.resource_key = &key;
    input.active_pointer = &pointer;
    input.store = &store;
    input.validate_local_mirror = false;

    auto validation = validate_startup_against_active_pointer(input);
    EXPECT_TRUE(validation.valid);
    EXPECT_EQ(validation.health, RuntimePolicyHealth::Active);

    destroy_db(db_path);
}

TEST(RuntimePolicyStartupValidationIntegration, CommittedYamlHashMismatchFails) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);

    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    TqPolicyDocument doc = make_policy("route-hash", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version;
    version.generation = 1;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    set_version_canonical_hash(version, emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    ActivePolicyPointer pointer;
    pointer.generation = 1;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash =
        "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    pointer.version_key = make_version_key(key, 1);
    pointer.yaml_key = make_yaml_key(key, 1);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    StartupValidationInput input{};
    input.resource_key = &key;
    input.active_pointer = &pointer;
    input.store = &store;
    input.validate_local_mirror = false;

    auto validation = validate_startup_against_active_pointer(input);
    EXPECT_FALSE(validation.valid);
    EXPECT_EQ(validation.health, RuntimePolicyHealth::InactiveActivePointerMissingTarget);

    destroy_db(db_path);
}

TEST(RuntimePolicyStartupValidationIntegration, RejectInactiveModeSetsRejectFlag) {
    const std::string db_path = make_temp_db_path();
    const std::string state_dir =
        (fs::current_path() / ("bt_startup_reject_" + std::to_string(std::random_device{}())))
            .string();
    destroy_db(db_path);
    fs::remove_all(state_dir);
    fs::create_directories(state_dir);

    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    TqPolicyDocument doc = make_policy("route-reject", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version;
    version.generation = 1;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    set_version_canonical_hash(version, emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    ActivePolicyPointer pointer;
    pointer.generation = 1;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = loaded.record.canonical_hash;
    pointer.version_key = make_version_key(key, 1);
    pointer.yaml_key = make_yaml_key(key, 1);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    TqPolicyDocument diverged_local = make_policy("route-diverged", 1);
    PolicyIrYamlEmitResult diverged_emit = emit_policy_ir_canonical_yaml(diverged_local);
    ASSERT_TRUE(diverged_emit.ok);

    LocalPolicyPersistenceConfig cfg;
    cfg.enabled = true;
    cfg.state_dir = state_dir;
    PersistedPolicyMetadata meta;
    meta.policy_identity = diverged_local.policy_id;
    meta.generation = 1;
    meta.metadata_schema_version = 1;
    meta.resource_key = key.to_string();
    meta.schema_version = 1;
    meta.api_version = diverged_local.api_version;
    meta.kind = diverged_local.kind;
    meta.active_policy_file = "active-policy.yaml";
    meta.source_type = "taperql-apply";
    meta.written_at_unix_epoch_ms = 123;
    meta.canonical_hash_algorithm = "sha256";
    ASSERT_TRUE(persist_active_policy_canonical_yaml(cfg, diverged_local, meta).ok);

    bytetaper::runtime::RuntimePolicyStore runtime_store;
    RuntimePolicyPlaneConfig config{};
    config.runtime_policy_store = &runtime_store;
    config.policy_state_store = &store;
    config.resource_key = key;
    config.persistence_config = cfg;
    config.inactive_config.when_inactive = PolicyInactiveMode::Reject;

    RuntimePolicyPlane plane(config);
    auto result = plane.start();
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::PolicyInactive);

    auto active = runtime_store.load();
    ASSERT_NE(active, nullptr);
    EXPECT_TRUE(active->reject_requests);
    EXPECT_EQ(active->routes.size(), 0u);

    destroy_db(db_path);
    fs::remove_all(state_dir);
}

TEST(RuntimePolicyStartupValidationIntegration, PassThroughInactiveModeInstallsEmptySnapshot) {
    const std::string db_path = make_temp_db_path();
    const std::string state_dir =
        (fs::current_path() / ("bt_startup_passthrough_" + std::to_string(std::random_device{}())))
            .string();
    destroy_db(db_path);
    fs::remove_all(state_dir);
    fs::create_directories(state_dir);

    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    TqPolicyDocument doc = make_policy("route-pass", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version;
    version.generation = 1;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    set_version_canonical_hash(version, emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    ActivePolicyPointer pointer;
    pointer.generation = 1;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = loaded.record.canonical_hash;
    pointer.version_key = make_version_key(key, 1);
    pointer.yaml_key = make_yaml_key(key, 1);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    LocalPolicyPersistenceConfig cfg;
    cfg.enabled = true;
    cfg.state_dir = state_dir;
    PersistedPolicyMetadata meta;
    meta.policy_identity = doc.policy_id;
    meta.generation = 1;
    meta.metadata_schema_version = 1;
    meta.resource_key = key.to_string();
    meta.schema_version = 1;
    meta.api_version = doc.api_version;
    meta.kind = doc.kind;
    meta.active_policy_file = "active-policy.yaml";
    meta.source_type = "taperql-apply";
    meta.written_at_unix_epoch_ms = 123;
    meta.canonical_hash_algorithm = "sha256";
    ASSERT_TRUE(persist_active_policy_canonical_yaml(cfg, doc, meta).ok);

    {
        const fs::path meta_path = fs::path(state_dir) / "active-policy.meta.json";
        std::ifstream meta_in(meta_path);
        std::string meta_json((std::istreambuf_iterator<char>(meta_in)),
                              std::istreambuf_iterator<char>());
        meta_in.close();
        const std::string needle = "\"canonicalHash\": \"sha256:";
        const std::size_t pos = meta_json.find(needle);
        ASSERT_NE(pos, std::string::npos);
        const std::size_t hash_start = pos + needle.size();
        const std::size_t hash_end = meta_json.find('"', hash_start);
        ASSERT_NE(hash_end, std::string::npos);
        meta_json.replace(hash_start, hash_end - hash_start,
                          "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        std::ofstream meta_out(meta_path, std::ios::trunc);
        meta_out << meta_json;
    }

    bytetaper::runtime::RuntimePolicyStore runtime_store;
    RuntimePolicyPlaneConfig config{};
    config.runtime_policy_store = &runtime_store;
    config.policy_state_store = &store;
    config.resource_key = key;
    config.persistence_config = cfg;
    config.inactive_config.when_inactive = PolicyInactiveMode::PassThrough;

    RuntimePolicyPlane plane(config);
    auto result = plane.start();
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::PolicyInactive);
    EXPECT_EQ(result.status.health, RuntimePolicyHealth::InactiveCorruptedLocal);
    EXPECT_EQ(result.diagnostics.loaded_source, "inactive");

    auto active = runtime_store.load();
    ASSERT_NE(active, nullptr);
    EXPECT_FALSE(active->reject_requests);
    EXPECT_EQ(active->routes.size(), 0u);
    EXPECT_EQ(active->policy_identity, "empty");
    EXPECT_NE(active->policy_identity, doc.policy_id);

    destroy_db(db_path);
    fs::remove_all(state_dir);
}

TEST(RuntimePolicyStartupValidationIntegration, StartupFailInactiveModeFailsStartup) {
    const std::string db_path = make_temp_db_path();
    const std::string state_dir =
        (fs::current_path() / ("bt_startup_fail_" + std::to_string(std::random_device{}())))
            .string();
    destroy_db(db_path);
    fs::remove_all(state_dir);
    fs::create_directories(state_dir);

    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    TqPolicyDocument doc = make_policy("route-fail", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version;
    version.generation = 1;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    set_version_canonical_hash(version, emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    ActivePolicyPointer pointer;
    pointer.generation = 1;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = loaded.record.canonical_hash;
    pointer.version_key = make_version_key(key, 1);
    pointer.yaml_key = make_yaml_key(key, 1);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    TqPolicyDocument diverged_local = make_policy("route-diverged-fail", 1);
    LocalPolicyPersistenceConfig cfg;
    cfg.enabled = true;
    cfg.state_dir = state_dir;
    PersistedPolicyMetadata meta;
    meta.policy_identity = diverged_local.policy_id;
    meta.generation = 1;
    meta.metadata_schema_version = 1;
    meta.resource_key = key.to_string();
    meta.schema_version = 1;
    meta.api_version = diverged_local.api_version;
    meta.kind = diverged_local.kind;
    meta.active_policy_file = "active-policy.yaml";
    meta.source_type = "taperql-apply";
    meta.written_at_unix_epoch_ms = 123;
    meta.canonical_hash_algorithm = "sha256";
    ASSERT_TRUE(persist_active_policy_canonical_yaml(cfg, diverged_local, meta).ok);

    bytetaper::runtime::RuntimePolicyStore runtime_store;
    RuntimePolicyPlaneConfig config{};
    config.runtime_policy_store = &runtime_store;
    config.policy_state_store = &store;
    config.resource_key = key;
    config.persistence_config = cfg;
    config.inactive_config.when_inactive = PolicyInactiveMode::StartupFail;

    RuntimePolicyPlane plane(config);
    auto result = plane.start();
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::StartupFailed);
    EXPECT_EQ(result.status.health, RuntimePolicyHealth::InactiveCorruptedLocal);
    EXPECT_EQ(result.diagnostics.error_code, "policy_inactive_startup_fail");

    EXPECT_EQ(runtime_store.load(), nullptr);

    destroy_db(db_path);
    fs::remove_all(state_dir);
}

TEST(RuntimePolicyStartupValidationIntegration, BootstrapDriftReportedOnValidStartup) {
    const std::string db_path = make_temp_db_path();
    const std::string state_dir =
        (fs::current_path() / ("bt_startup_drift_" + std::to_string(std::random_device{}())))
            .string();
    destroy_db(db_path);
    fs::remove_all(state_dir);
    fs::create_directories(state_dir);

    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    TqPolicyDocument committed = make_policy("committed-route", 1);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(committed);
    ASSERT_TRUE(emit.ok);

    TqPolicyDocument bootstrap_doc = make_policy("bootstrap-route", 1);
    PolicyIrYamlEmitResult bootstrap_emit = emit_policy_ir_canonical_yaml(bootstrap_doc);
    ASSERT_TRUE(bootstrap_emit.ok);
    const fs::path bootstrap_path = fs::path(state_dir) / "bootstrap.yaml";
    {
        std::ofstream out(bootstrap_path);
        out << bootstrap_emit.yaml;
    }

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    PolicyVersionRecord version;
    version.generation = 1;
    version.policy_id = committed.policy_id;
    version.schema_version = 1;
    version.api_version = committed.api_version;
    version.kind = committed.kind;
    version.source_type = "taperql-apply";
    set_version_canonical_hash(version, emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);

    auto loaded = store.load_policy_version(key, 1);
    ASSERT_TRUE(loaded.ok);

    ActivePolicyPointer pointer;
    pointer.generation = 1;
    pointer.policy_id = committed.policy_id;
    pointer.canonical_hash = loaded.record.canonical_hash;
    pointer.version_key = make_version_key(key, 1);
    pointer.yaml_key = make_yaml_key(key, 1);
    pointer.schema_version = 1;
    pointer.api_version = committed.api_version;
    pointer.kind = committed.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    LocalPolicyPersistenceConfig cfg;
    cfg.enabled = true;
    cfg.state_dir = state_dir;
    PersistedPolicyMetadata meta;
    meta.policy_identity = committed.policy_id;
    meta.generation = 1;
    meta.metadata_schema_version = 1;
    meta.resource_key = key.to_string();
    meta.schema_version = 1;
    meta.api_version = committed.api_version;
    meta.kind = committed.kind;
    meta.active_policy_file = "active-policy.yaml";
    meta.source_type = "taperql-apply";
    meta.written_at_unix_epoch_ms = 123;
    meta.canonical_hash_algorithm = "sha256";
    ASSERT_TRUE(persist_active_policy_canonical_yaml(cfg, committed, meta).ok);

    bytetaper::runtime::RuntimePolicyStore runtime_store;
    RuntimePolicyPlaneConfig plane_cfg{};
    plane_cfg.runtime_policy_store = &runtime_store;
    plane_cfg.policy_state_store = &store;
    plane_cfg.resource_key = key;
    plane_cfg.persistence_config = cfg;
    const std::string bootstrap_file = bootstrap_path.string();
    plane_cfg.bootstrap_policy_file = bootstrap_file.c_str();

    RuntimePolicyPlane plane(plane_cfg);
    auto result = plane.start();
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.diagnostics.bootstrap_drift_detected);
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::BootstrapDriftDetected);
    EXPECT_EQ(result.status.health, RuntimePolicyHealth::BootstrapDriftDetected);

    auto active = runtime_store.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->policy_identity, committed.policy_id);
    EXPECT_FALSE(active->reject_requests);

    destroy_db(db_path);
    fs::remove_all(state_dir);
}

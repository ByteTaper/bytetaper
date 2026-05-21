// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_config.h"
#include "control_plane/control_plane_service.h"
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
    return (fs::temp_directory_path() / ("bt_cp_restart_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    rocksdb::DestroyDB(path, rocksdb::Options{});
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation) {
    TqPolicyDocument doc;
    doc.document_id = "cp-restart-test";
    doc.source_name = "test";
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
    version.source_type = "yaml";
    version.canonical_hash = "sha256:" + compute_canonical_yaml_sha256_hex(emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);
}

void promote_generation(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                        const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    ActivePolicyPointer pointer;
    pointer.generation = generation;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = "sha256:" + compute_canonical_yaml_sha256_hex(emit.yaml);
    pointer.version_key = make_version_key(key, generation);
    pointer.yaml_key = make_yaml_key(key, generation);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "yaml";

    ExpectedActivePolicy expected;
    const auto active_res = store.load_active_pointer(key);
    if (active_res.ok) {
        expected.generation = active_res.pointer.generation;
        expected.policy_id = active_res.pointer.policy_id;
        pointer.previous_generation = active_res.pointer.generation;
        pointer.previous_policy_id = active_res.pointer.policy_id;
    } else {
        expected.generation = 0;
    }
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

} // namespace

TEST(ControlPlaneRestartDurabilityTest, ActivePointerSurvivesStoreReopen) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument doc_v1 = make_policy_doc("route-v1", 1);
    const TqPolicyDocument doc_v2 = make_policy_doc("route-v2", 2);

    {
        RocksDBPolicyStateStore store(db_path.c_str());
        ASSERT_TRUE(store.is_open());
        seed_generation(store, key, doc_v1, 1);
        promote_generation(store, key, doc_v1, 1);
        seed_generation(store, key, doc_v2, 2);
        promote_generation(store, key, doc_v2, 2);
    }

    RocksDBPolicyStateStore reopened(db_path.c_str());
    ASSERT_TRUE(reopened.is_open());

    const auto active = reopened.load_active_pointer(key);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 2u);
    EXPECT_EQ(active.pointer.policy_id, doc_v2.policy_id);

    const auto version = reopened.load_policy_version(key, 2);
    ASSERT_TRUE(version.ok);
    EXPECT_EQ(version.record.policy_id, doc_v2.policy_id);
}

TEST(ControlPlaneRestartDurabilityTest, ControlPlaneServiceReadsCommittedPolicyAfterReopen) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const TqPolicyDocument doc = make_policy_doc("route-restart", 1);

    {
        RocksDBPolicyStateStore store(db_path.c_str());
        ASSERT_TRUE(store.is_open());
        seed_generation(store, key, doc, 1);
        promote_generation(store, key, doc, 1);
    }

    ControlPlaneServiceConfig config{};
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    config.policy_state_store = &store;
    config.security.deployment_mode = ControlPlaneDeploymentMode::LocalDev;

    ControlPlaneService service(config);
    const ActivePolicyResult active = service.get_active_policy(key);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.active.generation, 1u);
    EXPECT_EQ(active.active.policy_id, doc.policy_id);

    const PolicyVersionListResult versions = service.list_policy_versions(key);
    ASSERT_TRUE(versions.ok);
    ASSERT_EQ(versions.versions.size(), 1u);
    EXPECT_EQ(versions.versions[0].generation, 1u);
    EXPECT_EQ(versions.versions[0].policy_id, doc.policy_id);

    destroy_db(db_path);
}

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/bootstrap_policy_importer.h"
#include "runtime_policy/runtime_policy_plane.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <random>
#include <rocksdb/db.h>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::runtime_policy;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_bootstrap_import_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    rocksdb::DestroyDB(path, rocksdb::Options{});
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id) {
    TqPolicyDocument doc;
    doc.document_id = "bootstrap-import";
    doc.source_name = "test";
    doc.expected_base_sha = "0000000000000000000000000000000000000000";
    doc.version.source_schema_version = "yaml/v1";
    doc.version.policy_ir_version = "tq-ir/v1";
    doc.version.identity_version = "policy-identity/v2";
    doc.generation = 1;
    doc.schema_version_num = 1;
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";

    TqRoutePolicy route;
    route.route_id = route_id;
    route.match_prefix = "/bootstrap";
    route.match_kind = TqRouteMatchKind::Prefix;
    route.mutation = TqMutationMode::Disabled;
    route.allowed_method = TqHttpMethod::Get;
    doc.routes.push_back(route);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

std::string write_bootstrap_file(const fs::path& dir, const TqPolicyDocument& doc,
                                 const std::string& name) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    EXPECT_TRUE(emit.ok);
    const fs::path path = dir / name;
    std::ofstream out(path);
    out << emit.yaml;
    out.close();
    return path.string();
}

class BootstrapImportFixture : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db(db_path_);
        state_dir_ =
            (fs::current_path() / ("bt_bootstrap_files_" + std::to_string(++seq_))).string();
        fs::remove_all(state_dir_);
        fs::create_directories(state_dir_);
        key_ = PolicyResourceKey::default_runtime();
        store_.emplace(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        persistence_.enabled = true;
        persistence_.state_dir = state_dir_;
        persistence_.active_policy_filename = "active-policy.yaml";
        persistence_.metadata_filename = "active-policy.meta.json";
    }

    void TearDown() override {
        store_.reset();
        destroy_db(db_path_);
        fs::remove_all(state_dir_);
    }

    BootstrapImportInput make_input(const std::string& bootstrap_path) {
        BootstrapImportInput input;
        input.bootstrap_policy_file = bootstrap_path.c_str();
        input.store = &*store_;
        input.persistence_config = &persistence_;
        input.resource_key = &key_;
        return input;
    }

    static inline std::uint64_t seq_ = 0;
    std::string db_path_;
    std::string state_dir_;
    PolicyResourceKey key_;
    std::optional<RocksDBPolicyStateStore> store_;
    LocalPolicyPersistenceConfig persistence_;
};

} // namespace

TEST_F(BootstrapImportFixture, NoActivePointerBootstrapImportsGeneration1) {
    TqPolicyDocument bootstrap = make_policy_doc("bootstrap-route");
    const std::string bootstrap_path =
        write_bootstrap_file(state_dir_, bootstrap, "bootstrap.yaml");

    auto result = import_bootstrap_policy(make_input(bootstrap_path));
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.health, RuntimePolicyHealth::BootstrapImported);

    auto pointer = store_->load_active_pointer(key_);
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(pointer.pointer.generation, 1u);
}

TEST_F(BootstrapImportFixture, ActivePointerExistsMatchingBootstrapSkippedWithoutDrift) {
    TqPolicyDocument bootstrap = make_policy_doc("bootstrap-route");
    const std::string bootstrap_path =
        write_bootstrap_file(state_dir_, bootstrap, "bootstrap.yaml");

    ASSERT_TRUE(import_bootstrap_policy(make_input(bootstrap_path)).ok);
    const std::uint64_t gen_before = store_->load_active_pointer(key_).pointer.generation;

    auto second = import_bootstrap_policy(make_input(bootstrap_path));
    EXPECT_FALSE(second.ok);
    EXPECT_EQ(second.health, RuntimePolicyHealth::Active);
    EXPECT_NE(second.health, RuntimePolicyHealth::BootstrapDriftDetected);

    auto pointer = store_->load_active_pointer(key_);
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(pointer.pointer.generation, gen_before);
}

TEST_F(BootstrapImportFixture, ChangedBootstrapDoesNotOverwriteCommitted) {
    TqPolicyDocument bootstrap_a = make_policy_doc("bootstrap-a");
    const std::string path_a = write_bootstrap_file(state_dir_, bootstrap_a, "bootstrap-a.yaml");
    ASSERT_TRUE(import_bootstrap_policy(make_input(path_a)).ok);

    auto committed = store_->load_active_pointer(key_);
    ASSERT_TRUE(committed.ok);

    TqPolicyDocument bootstrap_b = make_policy_doc("bootstrap-b");
    const std::string path_b = write_bootstrap_file(state_dir_, bootstrap_b, "bootstrap-b.yaml");
    auto blocked = import_bootstrap_policy(make_input(path_b));
    EXPECT_FALSE(blocked.ok);
    EXPECT_EQ(blocked.health, RuntimePolicyHealth::BootstrapDriftDetected);

    auto after = store_->load_active_pointer(key_);
    ASSERT_TRUE(after.ok);
    EXPECT_EQ(after.pointer.generation, committed.pointer.generation);
    EXPECT_EQ(after.pointer.policy_id, committed.pointer.policy_id);
    EXPECT_EQ(after.pointer.canonical_hash, committed.pointer.canonical_hash);
}

TEST_F(BootstrapImportFixture, BootstrapImportWritesActiveFileMirror) {
    TqPolicyDocument bootstrap = make_policy_doc("bootstrap-route");
    const std::string bootstrap_path =
        write_bootstrap_file(state_dir_, bootstrap, "bootstrap.yaml");

    ASSERT_TRUE(import_bootstrap_policy(make_input(bootstrap_path)).ok);
    EXPECT_TRUE(fs::exists(fs::path(state_dir_) / "active-policy.yaml"));
    EXPECT_TRUE(fs::exists(fs::path(state_dir_) / "active-policy.meta.json"));
}

TEST(RuntimePolicyBootstrapImportPlane, NoActivePointerNoBootstrapInstallsEmpty) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);

    RocksDBPolicyStateStore policy_store(db_path.c_str());
    ASSERT_TRUE(policy_store.is_open());

    bytetaper::runtime::RuntimePolicyStore runtime_store;
    RuntimePolicyPlaneConfig config;
    config.runtime_policy_store = &runtime_store;
    config.policy_state_store = &policy_store;

    RuntimePolicyPlane plane(config);
    auto result = plane.start();
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.status.health, RuntimePolicyHealth::NoPolicyConfigured);

    destroy_db(db_path);
}

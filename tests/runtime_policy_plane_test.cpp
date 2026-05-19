// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/policy_snapshot.h"
#include "runtime_policy/runtime_policy_plane.h"
#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>

namespace fs = std::filesystem;
using namespace bytetaper::runtime_policy;
using namespace bytetaper::taperquery;

namespace {

class RuntimePolicyPlaneTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<uint64_t> dist(1000000, 9999999);
        uint64_t suffix = dist(generator);

        state_dir_ = (fs::current_path() / ("tmp_rpp_test_" + std::to_string(suffix))).string();
        fs::remove_all(state_dir_);
        fs::create_directories(state_dir_);
    }

    void TearDown() override {
        fs::remove_all(state_dir_);
    }

    TqPolicyDocument make_policy(const std::string& route_id, const std::string& prefix) {
        TqPolicyDocument doc;
        doc.document_id = "test-doc";
        doc.source_name = "test";
        doc.expected_base_sha = "0000000000000000000000000000000000000000";
        doc.version.source_schema_version = "yaml/v1";
        doc.version.policy_ir_version = "tq-ir/v1";
        doc.version.identity_version = "policy-identity/v2";

        TqRoutePolicy r;
        r.route_id = route_id;
        r.match_prefix = prefix;
        r.match_kind = TqRouteMatchKind::Prefix;
        r.mutation = TqMutationMode::Disabled;
        r.allowed_method = TqHttpMethod::Get;
        doc.routes.push_back(r);

        return doc;
    }

    std::string write_bootstrap_policy(const TqPolicyDocument& doc, const std::string& filename) {
        fs::path p = fs::path(state_dir_) / filename;
        PolicyIrYamlEmitResult emit_res = emit_policy_ir_canonical_yaml(doc);
        EXPECT_TRUE(emit_res.ok);

        std::ofstream out(p);
        out << emit_res.yaml;
        out.close();
        return p.string();
    }

    void write_persisted_policy(const TqPolicyDocument& doc, std::uint64_t generation) {
        LocalPolicyPersistenceConfig persist_cfg;
        persist_cfg.enabled = true;
        persist_cfg.state_dir = state_dir_;
        persist_cfg.active_policy_filename = "active-policy.yaml";
        persist_cfg.metadata_filename = "active-policy.meta.json";

        PersistedPolicyMetadata meta;
        meta.policy_identity = compute_policy_document_identity(doc);
        meta.generation = generation;
        meta.source_type = "admin-api";
        meta.written_at_unix_epoch_ms = 987654321;
        meta.operator_id = "admin";
        meta.request_id = "req-test";

        auto persist_res = persist_active_policy_canonical_yaml(persist_cfg, doc, meta);
        ASSERT_TRUE(persist_res.ok) << persist_res.error;
    }

    RuntimePolicyPlaneConfig make_config(bytetaper::runtime::RuntimePolicyStore* store,
                                         const char* bootstrap_file = nullptr,
                                         bool persistence_enabled = false) {

        RuntimePolicyPlaneConfig config{};
        config.bootstrap_policy_file = bootstrap_file;
        config.runtime_policy_store = store;
        config.persistence_config.enabled = persistence_enabled;
        config.persistence_config.state_dir = persistence_enabled ? state_dir_ : "";
        config.persistence_config.active_policy_filename = "active-policy.yaml";
        config.persistence_config.metadata_filename = "active-policy.meta.json";
        return config;
    }

    std::string state_dir_;
};

} // namespace

TEST_F(RuntimePolicyPlaneTest, StartsFromPersistedPolicy) {
    // Setup: write a persisted policy
    TqPolicyDocument policy = make_policy("persisted-route", "/persisted");
    std::string persisted_identity = compute_policy_document_identity(policy);
    write_persisted_policy(policy, 7);

    // Also set a bootstrap file (should be ignored in favour of persisted)
    TqPolicyDocument bootstrap = make_policy("bootstrap-route", "/bootstrap");
    std::string bootstrap_path = write_bootstrap_policy(bootstrap, "bootstrap.yaml");

    bytetaper::runtime::RuntimePolicyStore store;
    auto config = make_config(&store, bootstrap_path.c_str(), /*persistence_enabled=*/true);

    RuntimePolicyPlane plane(config);
    auto result = plane.start();

    ASSERT_TRUE(result.ok) << result.status.error_message;
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::LoadedFromPersistedPolicy);
    EXPECT_EQ(result.diagnostics.loaded_source, "persisted");
    EXPECT_EQ(result.diagnostics.generation, 7u);
    EXPECT_EQ(result.diagnostics.policy_identity, persisted_identity);

    // Verify the store holds the persisted snapshot
    auto active = store.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->policy_identity, persisted_identity);
    EXPECT_EQ(active->generation, 7u);
}

TEST_F(RuntimePolicyPlaneTest, FallsBackToBootstrapWhenNoPersisted) {
    // Persistence enabled but no persisted file exists → should load bootstrap
    TqPolicyDocument bootstrap = make_policy("bootstrap-route", "/bootstrap");
    std::string bootstrap_identity = compute_policy_document_identity(bootstrap);
    std::string bootstrap_path = write_bootstrap_policy(bootstrap, "bootstrap.yaml");

    bytetaper::runtime::RuntimePolicyStore store;
    auto config = make_config(&store, bootstrap_path.c_str(), /*persistence_enabled=*/true);

    RuntimePolicyPlane plane(config);
    auto result = plane.start();

    ASSERT_TRUE(result.ok) << result.status.error_message;
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::LoadedFromBootstrapPolicy);
    EXPECT_EQ(result.diagnostics.loaded_source, "bootstrap");

    auto active = store.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->routes.size(), 1u);
}

TEST_F(RuntimePolicyPlaneTest, FailsStartupOnCorruptPersisted) {
    // Write corrupt files
    fs::path active_yaml = fs::path(state_dir_) / "active-policy.yaml";
    fs::path active_meta = fs::path(state_dir_) / "active-policy.meta.json";
    {
        std::ofstream f(active_yaml);
        f << "CORRUPT GARBAGE %%%\n";
    }
    {
        std::ofstream f(active_meta);
        f << "{\"policy_identity\": \"bad\", \"canonical_yaml_sha256\": \"wrong\"}\n";
    }

    bytetaper::runtime::RuntimePolicyStore store;
    auto config = make_config(&store, nullptr, /*persistence_enabled=*/true);

    RuntimePolicyPlane plane(config);
    auto result = plane.start();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::StartupFailed);
    EXPECT_FALSE(result.status.error_message.empty());
}

TEST_F(RuntimePolicyPlaneTest, LoadsEmptyPolicyWhenNothingConfigured) {
    // No bootstrap file, no persistence
    bytetaper::runtime::RuntimePolicyStore store;
    auto config = make_config(&store, nullptr, /*persistence_enabled=*/false);

    RuntimePolicyPlane plane(config);
    auto result = plane.start();

    ASSERT_TRUE(result.ok) << result.status.error_message;
    EXPECT_EQ(result.status.code, RuntimePolicyStatusCode::LoadedEmptyPolicy);
    EXPECT_EQ(result.diagnostics.loaded_source, "empty");

    auto active = store.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->routes.size(), 0u);
}

TEST_F(RuntimePolicyPlaneTest, ReportsLoadedSourceInDiagnostics) {
    // Persisted policy load → diagnostics.loaded_source == "persisted"
    TqPolicyDocument policy = make_policy("diag-route", "/diag");
    write_persisted_policy(policy, 3);

    bytetaper::runtime::RuntimePolicyStore store;
    auto config = make_config(&store, nullptr, /*persistence_enabled=*/true);

    RuntimePolicyPlane plane(config);
    auto result = plane.start();

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.diagnostics.loaded_source, "persisted");
    EXPECT_EQ(result.diagnostics.generation, 3u);
    EXPECT_TRUE(result.diagnostics.persistence_enabled);
    EXPECT_EQ(result.diagnostics.policy_state_dir, state_dir_);
}

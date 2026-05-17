// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>

using namespace bytetaper::taperquery;
namespace fs = std::filesystem;

namespace {

class StartupRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<uint64_t> dist(1000000, 9999999);
        uint64_t suffix = dist(generator);

        state_dir_ = (fs::current_path() / ("tmp_startup_test_" + std::to_string(suffix))).string();
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

    std::string state_dir_;
};

} // namespace

TEST_F(StartupRecoveryTest, RedeployLoadsPersistedNotBootstrap) {
    // 1. Setup bootstrap policy (Policy A)
    TqPolicyDocument policy_a = make_policy("bootstrap-route", "/bootstrap");
    std::string bootstrap_path = write_bootstrap_policy(policy_a, "bootstrap.yaml");
    std::string identity_a = compute_policy_document_identity(policy_a);

    // 2. Setup persisted policy (Policy B)
    TqPolicyDocument policy_b = make_policy("applied-route", "/applied");
    std::string identity_b = compute_policy_document_identity(policy_b);

    LocalPolicyPersistenceConfig persist_cfg;
    persist_cfg.enabled = true;
    persist_cfg.state_dir = state_dir_;
    persist_cfg.active_policy_filename = "active-policy.yaml";
    persist_cfg.metadata_filename = "active-policy.meta.json";

    PersistedPolicyMetadata meta;
    meta.policy_identity = identity_b;
    meta.generation = 5;
    meta.source_type = "admin-api";
    meta.written_at_unix_epoch_ms = 987654321;
    meta.operator_id = "admin";
    meta.request_id = "req-999";

    // Write B to state_dir
    auto persist_res = persist_active_policy_canonical_yaml(persist_cfg, policy_b, meta);
    ASSERT_TRUE(persist_res.ok);

    // Record SHA of active-policy.yaml before load
    std::string active_yaml_path = (fs::path(state_dir_) / "active-policy.yaml").string();
    std::ifstream yaml_in(active_yaml_path);
    std::stringstream buf;
    buf << yaml_in.rdbuf();
    std::string content_before = buf.str();
    yaml_in.close();

    // Record content of active-policy.meta.json before load
    std::string active_meta_path = (fs::path(state_dir_) / "active-policy.meta.json").string();
    std::ifstream meta_in(active_meta_path);
    std::stringstream meta_buf;
    meta_buf << meta_in.rdbuf();
    std::string meta_content_before = meta_buf.str();
    meta_in.close();

    // 3. Run load_startup_policy_with_persistence
    StartupPolicyLoadConfig startup_cfg;
    startup_cfg.bootstrap_policy_file = bootstrap_path;
    startup_cfg.policy_state_dir = state_dir_;
    startup_cfg.active_policy_filename = "active-policy.yaml";
    startup_cfg.metadata_filename = "active-policy.meta.json";
    startup_cfg.policy_persistence_enabled = true;
    startup_cfg.fallback_to_bootstrap_on_persisted_policy_error = false;

    auto result = load_startup_policy_with_persistence(startup_cfg);

    // 4. Assertions
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.loaded_source, "persisted");
    EXPECT_EQ(result.policy_identity, identity_b);
    EXPECT_EQ(result.generation, 5);
    EXPECT_NE(result.policy_identity, identity_a);

    // Assert that active-policy.yaml is not overwritten
    std::ifstream yaml_in_after(active_yaml_path);
    std::stringstream buf_after;
    buf_after << yaml_in_after.rdbuf();
    std::string content_after = buf_after.str();
    yaml_in_after.close();
    EXPECT_EQ(content_before, content_after);

    // Assert that active-policy.meta.json is not overwritten
    std::ifstream meta_in_after(active_meta_path);
    std::stringstream meta_buf_after;
    meta_buf_after << meta_in_after.rdbuf();
    std::string meta_content_after = meta_buf_after.str();
    meta_in_after.close();
    EXPECT_EQ(meta_content_before, meta_content_after);
}

TEST_F(StartupRecoveryTest, MissingPersistedFileFallsBackToBootstrap) {
    // 1. Setup bootstrap policy (Policy A)
    TqPolicyDocument policy_a = make_policy("bootstrap-route", "/bootstrap");
    std::string bootstrap_path = write_bootstrap_policy(policy_a, "bootstrap.yaml");
    std::string identity_a = compute_policy_document_identity(policy_a);

    // 2. State directory is empty (no persisted files)
    StartupPolicyLoadConfig startup_cfg;
    startup_cfg.bootstrap_policy_file = bootstrap_path;
    startup_cfg.policy_state_dir = state_dir_;
    startup_cfg.active_policy_filename = "active-policy.yaml";
    startup_cfg.metadata_filename = "active-policy.meta.json";
    startup_cfg.policy_persistence_enabled = true;
    startup_cfg.fallback_to_bootstrap_on_persisted_policy_error = false;

    auto result = load_startup_policy_with_persistence(startup_cfg);

    // 3. Assertions
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.loaded_source, "bootstrap");
    EXPECT_EQ(result.policy_identity, identity_a);
    EXPECT_EQ(result.generation, 1);

    // Verify no persistent files were written
    EXPECT_FALSE(fs::exists(fs::path(state_dir_) / "active-policy.yaml"));
    EXPECT_FALSE(fs::exists(fs::path(state_dir_) / "active-policy.meta.json"));
}

TEST_F(StartupRecoveryTest, CorruptPersistedFileFails) {
    // 1. Setup bootstrap policy (Policy A)
    TqPolicyDocument policy_a = make_policy("bootstrap-route", "/bootstrap");
    std::string bootstrap_path = write_bootstrap_policy(policy_a, "bootstrap.yaml");

    // 2. Write corrupt files directly
    fs::path active_yaml = fs::path(state_dir_) / "active-policy.yaml";
    fs::path active_meta = fs::path(state_dir_) / "active-policy.meta.json";

    {
        std::ofstream yf(active_yaml);
        yf << "CORRUPT GARBAGE %%%\n";
    }
    {
        std::ofstream mf(active_meta);
        mf << "{\"policy_identity\": \"invalid\", \"canonical_yaml_sha256\": \"wrong\"}\n";
    }

    // 3. Run load_startup_policy_with_persistence
    StartupPolicyLoadConfig startup_cfg;
    startup_cfg.bootstrap_policy_file = bootstrap_path;
    startup_cfg.policy_state_dir = state_dir_;
    startup_cfg.active_policy_filename = "active-policy.yaml";
    startup_cfg.metadata_filename = "active-policy.meta.json";
    startup_cfg.policy_persistence_enabled = true;
    startup_cfg.fallback_to_bootstrap_on_persisted_policy_error = false;

    auto result = load_startup_policy_with_persistence(startup_cfg);

    // 4. Assertions
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("corrupt"), std::string::npos);

    // Verify the corrupt file was NOT overwritten
    std::ifstream yf_in(active_yaml);
    std::string line;
    std::getline(yf_in, line);
    EXPECT_EQ(line, "CORRUPT GARBAGE %%%");
}

TEST_F(StartupRecoveryTest, ExplicitFallbackModeLoadsBootstrapOnCorrupt) {
    // 1. Setup bootstrap policy (Policy A)
    TqPolicyDocument policy_a = make_policy("bootstrap-route", "/bootstrap");
    std::string bootstrap_path = write_bootstrap_policy(policy_a, "bootstrap.yaml");
    std::string identity_a = compute_policy_document_identity(policy_a);

    // 2. Write corrupt files directly
    fs::path active_yaml = fs::path(state_dir_) / "active-policy.yaml";
    fs::path active_meta = fs::path(state_dir_) / "active-policy.meta.json";

    {
        std::ofstream yf(active_yaml);
        yf << "CORRUPT GARBAGE %%%\n";
    }
    {
        std::ofstream mf(active_meta);
        mf << "{\"policy_identity\": \"invalid\", \"canonical_yaml_sha256\": \"wrong\"}\n";
    }

    // 3. Run load_startup_policy_with_persistence WITH
    // fallback_to_bootstrap_on_persisted_policy_error = true
    StartupPolicyLoadConfig startup_cfg;
    startup_cfg.bootstrap_policy_file = bootstrap_path;
    startup_cfg.policy_state_dir = state_dir_;
    startup_cfg.active_policy_filename = "active-policy.yaml";
    startup_cfg.metadata_filename = "active-policy.meta.json";
    startup_cfg.policy_persistence_enabled = true;
    startup_cfg.fallback_to_bootstrap_on_persisted_policy_error = true;

    auto result = load_startup_policy_with_persistence(startup_cfg);

    // 4. Assertions
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.loaded_source, "bootstrap");
    EXPECT_EQ(result.policy_identity, identity_a);
    EXPECT_EQ(result.generation, 1);

    // Verify the corrupt file was NOT overwritten
    std::ifstream yf_in(active_yaml);
    std::string line;
    std::getline(yf_in, line);
    EXPECT_EQ(line, "CORRUPT GARBAGE %%%");
}

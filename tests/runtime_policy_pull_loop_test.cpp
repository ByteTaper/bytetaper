// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "operational/policy_activation_barrier.h"
#include "runtime/policy_snapshot.h"
#include "runtime/route_cache_epoch_store.h"
#include "runtime_policy/control_plane_policy_client.h"
#include "runtime_policy/runtime_policy_pull_loop.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"
#include "taperquery/tq_cache_namespace_versioning.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <random>

namespace fs = std::filesystem;
using namespace bytetaper::cache;
using namespace bytetaper::control_plane;
using namespace bytetaper::operational;
using namespace bytetaper::runtime;
using namespace bytetaper::runtime_policy;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_pull_test_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation,
                                 const std::vector<std::string>& fields = { "id" }) {
    TqPolicyDocument doc;
    doc.document_id = "pull-test";
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
    route.field_filter.mode = TqFieldFilterMode::Allowlist;
    route.field_filter.fields = fields;
    doc.routes.push_back(route);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

std::string canonical_hash_for_yaml(const std::string& yaml) {
    return "sha256:" + compute_canonical_yaml_sha256_hex(yaml);
}

RuntimePolicySnapshotBuildResult failing_snapshot_build_hook(const TqPolicyDocument&,
                                                             std::uint64_t) {
    RuntimePolicySnapshotBuildResult result{};
    result.ok = false;
    result.error = "injected snapshot build failure";
    return result;
}

void store_version(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
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

void promote_active(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
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

    const auto active_res = store.load_active_pointer(key);
    ExpectedActivePolicy expected;
    if (active_res.ok) {
        expected.generation = active_res.pointer.generation;
        expected.policy_id = active_res.pointer.policy_id;
    } else {
        expected.generation = 0;
    }
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

class RecordingCleanupQueue : public RouteCacheCleanupQueue {
public:
    void enqueue(const RouteCacheCleanupJob&) override {}
};

class FakeControlPlanePolicyClient : public ControlPlanePolicyClient {
public:
    ActivePolicyPointerResult get_active_policy(const PolicyResourceKey&) override {
        get_active_calls++;
        if (fail_active) {
            ActivePolicyPointerResult out{};
            out.error = "control plane unavailable";
            out.error_code = kErrControlPlaneUnavailable;
            return out;
        }
        ActivePolicyPointerResult out{};
        out.ok = true;
        out.active = active_pointer;
        return out;
    }

    CanonicalPolicyResult get_policy_version(const PolicyResourceKey&, std::uint64_t) override {
        get_version_calls++;
        if (fail_version) {
            CanonicalPolicyResult out{};
            out.error = "fetch failed";
            out.error_code = kErrPolicyVersionFetchFailed;
            return out;
        }
        CanonicalPolicyResult out{};
        out.ok = true;
        out.record = version_record;
        out.canonical_yaml = canonical_yaml;
        return out;
    }

    RuntimeStatusReportResult
    report_runtime_status(const RuntimePolicyStatusReport& report) override {
        report_calls++;
        last_report = report;
        RuntimeStatusReportResult out{};
        out.ok = true;
        return out;
    }

    ActivePolicyPointer active_pointer{};
    PolicyVersionRecord version_record{};
    std::string canonical_yaml;
    bool fail_active = false;
    bool fail_version = false;
    int get_active_calls = 0;
    int get_version_calls = 0;
    int report_calls = 0;
    RuntimePolicyStatusReport last_report{};
};

class RuntimePolicyPullLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        mirror_dir_ = (fs::temp_directory_path() / ("bt_pull_mirror_" + db_path_)).string();
        destroy_db(db_path_);
        fs::remove_all(mirror_dir_);
        store_.emplace(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        key_ = PolicyResourceKey::default_runtime();

        const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
        store_version(*store_, key_, gen1, 1);
        promote_active(*store_, key_, gen1, 1);

        RuntimePolicySnapshotBuildResult initial = build_runtime_policy_snapshot_from_ir(gen1, 1);
        ASSERT_TRUE(initial.ok);
        ASSERT_TRUE(runtime_store_.install_initial(initial.snapshot, nullptr));

        l1_init(&l1_cache_);
        route_cache_epoch_register(&epoch_store_, "route-a");

        fake_client_ = std::make_unique<FakeControlPlanePolicyClient>();
        fake_client_->active_pointer.generation = 1;
        fake_client_->active_pointer.policy_id = gen1.policy_id;
        PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen1);
        ASSERT_TRUE(emit.ok);
        fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
        fake_client_->canonical_yaml = emit.yaml;
        fake_client_->version_record.generation = 1;
        fake_client_->version_record.policy_id = gen1.policy_id;
        fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;

        pull_config_.pull.enabled = true;
        pull_config_.pull.resource_key = key_;
        pull_config_.pull.local_mirror.enabled = true;
        pull_config_.pull.local_mirror.state_dir = mirror_dir_;
        pull_config_.pull.startup_fetch_timeout_ms = 0;
        pull_config_.client = fake_client_.get();
        pull_config_.runtime_policy_store = &runtime_store_;
        pull_config_.activation_barrier.policy_state_store = &*store_;
        pull_config_.activation_barrier.runtime_policy_store = &runtime_store_;
        pull_config_.activation_barrier.route_cache_epoch_store = &epoch_store_;
        pull_config_.activation_barrier.l1_cache = &l1_cache_;
        pull_config_.activation_barrier.l2_cleanup_queue = &cleanup_queue_;
        pull_config_.activation_barrier.resource_key = key_;
    }

    void TearDown() override {
        store_.reset();
        destroy_db(db_path_);
        fs::remove_all(mirror_dir_);
    }

    RuntimePolicyPullLoop make_loop() {
        return RuntimePolicyPullLoop(pull_config_);
    }

    std::string db_path_;
    std::string mirror_dir_;
    PolicyResourceKey key_;
    std::optional<RocksDBPolicyStateStore> store_;
    RuntimePolicyStore runtime_store_;
    RouteCacheEpochStore epoch_store_{};
    L1Cache l1_cache_{};
    RecordingCleanupQueue cleanup_queue_;
    std::unique_ptr<FakeControlPlanePolicyClient> fake_client_;
    RuntimePolicyPullLoopConfig pull_config_{};
};

TEST_F(RuntimePolicyPullLoopTest, RemoteGenerationEqualsLocalIsNoOp) {
    auto loop = make_loop();
    loop.tick();

    EXPECT_EQ(fake_client_->get_version_calls, 0);
    EXPECT_EQ(fake_client_->get_active_calls, 1);
    const auto st = loop.status();
    EXPECT_EQ(st.state, RuntimePolicyPullState::Active);
}

TEST_F(RuntimePolicyPullLoopTest, RemoteGenerationNewerFetchesAndActivates) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);

    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);
    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    fake_client_->active_pointer.previous_generation = 1;
    fake_client_->canonical_yaml = emit.yaml;
    fake_client_->version_record.generation = 2;
    fake_client_->version_record.policy_id = gen2.policy_id;
    fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;
    fake_client_->get_version_calls = 0;

    auto loop = make_loop();
    loop.tick();

    EXPECT_GE(fake_client_->get_version_calls, 1);
    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 2u);
    EXPECT_TRUE(fs::exists(fs::path(mirror_dir_) / "active-policy.yaml"));
}

TEST_F(RuntimePolicyPullLoopTest, HashMismatchRejectsActivation) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);

    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = "sha256:deadbeef";
    fake_client_->canonical_yaml = emit.yaml;

    auto loop = make_loop();
    loop.tick();

    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 1u);
    EXPECT_EQ(loop.status().last_error_code, kErrPolicyCanonicalHashMismatch);
    EXPECT_GE(fake_client_->report_calls, 1);
    EXPECT_EQ(fake_client_->last_report.activation_status, "activation_failed");
    EXPECT_EQ(fake_client_->last_report.active_generation, 1u);
    EXPECT_EQ(fake_client_->last_report.last_error_code, kErrPolicyCanonicalHashMismatch);

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    PolicyIrYamlEmitResult emit1 = emit_policy_ir_canonical_yaml(gen1);
    ASSERT_TRUE(emit1.ok);
    EXPECT_FALSE(fake_client_->last_report.active_canonical_hash.empty());
    EXPECT_EQ(fake_client_->last_report.active_canonical_hash, canonical_hash_for_yaml(emit1.yaml));
    EXPECT_EQ(fake_client_->last_report.active_policy_id, gen1.policy_id);
}

TEST_F(RuntimePolicyPullLoopTest, GenerationMismatchRejectsActivation) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);

    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    fake_client_->canonical_yaml = emit.yaml;
    fake_client_->version_record.generation = 3;
    fake_client_->version_record.policy_id = gen2.policy_id;
    fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;

    auto loop = make_loop();
    loop.tick();

    const auto active = runtime_store_.load();
    EXPECT_EQ(active->generation, 1u);
    EXPECT_EQ(loop.status().last_error_code, kErrPolicyGenerationMismatch);
}

TEST_F(RuntimePolicyPullLoopTest, FetchedPolicyActivatesWhenStoreMissingVersion) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);

    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    fake_client_->active_pointer.previous_generation = 1;
    fake_client_->canonical_yaml = emit.yaml;
    fake_client_->version_record.generation = 2;
    fake_client_->version_record.policy_id = gen2.policy_id;
    fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;

    auto loop = make_loop();
    loop.tick();

    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 2u);
    EXPECT_EQ(active->policy_identity, gen2.policy_id);
}

TEST_F(RuntimePolicyPullLoopTest, PolicyIdMismatchRejectsActivation) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2);
    store_version(*store_, key_, gen2, 2);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);

    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = "sha256:wrong_policy_id";
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    fake_client_->canonical_yaml = emit.yaml;

    auto loop = make_loop();
    loop.tick();

    const auto active = runtime_store_.load();
    EXPECT_EQ(active->generation, 1u);
    EXPECT_EQ(loop.status().last_error_code, kErrPolicyIdMismatch);
}

TEST_F(RuntimePolicyPullLoopTest, InvalidYamlCompileFailurePreservesOldSnapshot) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);

    const std::string invalid_yaml = "invalid: yaml: [[";
    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(invalid_yaml);
    fake_client_->canonical_yaml = invalid_yaml;
    fake_client_->version_record.generation = 2;
    fake_client_->version_record.policy_id = gen2.policy_id;
    fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;

    auto loop = make_loop();
    loop.tick();

    const auto active = runtime_store_.load();
    EXPECT_EQ(active->generation, 1u);
    EXPECT_EQ(loop.status().last_error_code, kErrPolicyCompileFailed);
    EXPECT_EQ(loop.status().state, RuntimePolicyPullState::ActivationFailed);
    EXPECT_FALSE(loop.status().last_control_plane_error.empty());
}

TEST_F(RuntimePolicyPullLoopTest, SnapshotBuildCompileFailurePreservesOldSnapshot) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);

    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    fake_client_->canonical_yaml = emit.yaml;
    fake_client_->version_record.generation = 2;
    fake_client_->version_record.policy_id = gen2.policy_id;
    fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;

    pull_config_.snapshot_build_fn = failing_snapshot_build_hook;
    RuntimePolicyPullLoop loop(pull_config_);
    loop.tick();

    const auto active = runtime_store_.load();
    EXPECT_EQ(active->generation, 1u);
    EXPECT_EQ(loop.status().last_error_code, kErrPolicyCompileFailed);
    EXPECT_EQ(loop.status().state, RuntimePolicyPullState::ActivationFailed);
    EXPECT_FALSE(loop.status().last_control_plane_error.empty());
}

TEST_F(RuntimePolicyPullLoopTest, ActivationBarrierFailurePreservesOldSnapshot) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);

    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    fake_client_->canonical_yaml = emit.yaml;
    fake_client_->version_record.generation = 2;
    fake_client_->version_record.policy_id = gen2.policy_id;
    fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;

    RouteCacheEpochStore broken_epoch_store{};
    pull_config_.activation_barrier.route_cache_epoch_store = &broken_epoch_store;

    RuntimePolicyPullLoop loop(pull_config_);
    loop.tick();

    const auto active = runtime_store_.load();
    EXPECT_EQ(active->generation, 1u);
    EXPECT_EQ(loop.status().last_error_code, kErrPolicyActivationFailed);
    EXPECT_GE(fake_client_->report_calls, 1);
    EXPECT_EQ(fake_client_->last_report.activation_status, "activation_failed");
}

TEST_F(RuntimePolicyPullLoopTest, SuccessfulActivationWritesLocalMirrorMetadata) {
    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2, { "id", "name" });
    store_version(*store_, key_, gen2, 2);

    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit.ok);
    fake_client_->active_pointer.generation = 2;
    fake_client_->active_pointer.policy_id = gen2.policy_id;
    fake_client_->active_pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    fake_client_->canonical_yaml = emit.yaml;
    fake_client_->version_record.generation = 2;
    fake_client_->version_record.policy_id = gen2.policy_id;
    fake_client_->version_record.canonical_hash = fake_client_->active_pointer.canonical_hash;

    auto loop = make_loop();
    loop.tick();

    ASSERT_TRUE(fs::exists(fs::path(mirror_dir_) / "active-policy.yaml"));
    ASSERT_TRUE(fs::exists(fs::path(mirror_dir_) / "active-policy.meta.json"));
    const auto loaded = load_persisted_active_policy(pull_config_.pull.local_mirror);
    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.metadata.generation, 2u);
    EXPECT_EQ(loaded.metadata.policy_identity, gen2.policy_id);
}

TEST_F(RuntimePolicyPullLoopTest, ControlPlaneUnavailableContinuesLastKnownGood) {
    fake_client_->fail_active = true;
    auto loop = make_loop();
    loop.tick();

    const auto active = runtime_store_.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 1u);
    EXPECT_FALSE(loop.status().control_plane_reachable);
    EXPECT_EQ(loop.status().state, RuntimePolicyPullState::DegradedControlPlaneUnavailable);
    EXPECT_GE(fake_client_->report_calls, 1);
    EXPECT_EQ(fake_client_->last_report.activation_status, "control_plane_unavailable");
}

TEST_F(RuntimePolicyPullLoopTest, NoLocalPolicyAndControlPlaneUnavailable) {
    RuntimePolicyStore empty_store;
    pull_config_.runtime_policy_store = &empty_store;
    fake_client_->fail_active = true;
    auto loop = make_loop();
    loop.tick();
    EXPECT_EQ(loop.status().state, RuntimePolicyPullState::PolicyInactive);
}

TEST_F(RuntimePolicyPullLoopTest, StatusReportContainsActiveGenerationAndPolicyId) {
    auto loop = make_loop();
    loop.tick();
    EXPECT_GE(fake_client_->report_calls, 1);
    EXPECT_EQ(fake_client_->last_report.active_generation, 1u);
    EXPECT_FALSE(fake_client_->last_report.active_policy_id.empty());
}

} // namespace

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_service.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "runtime/policy_snapshot.h"
#include "runtime/route_cache_epoch_store.h"
#include "runtime_policy/control_plane_policy_client.h"
#include "runtime_policy/runtime_policy_pull_loop.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::runtime;
using namespace bytetaper::runtime_policy;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_pull_integ_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation,
                                 const std::vector<std::string>& fields = {}) {
    TqPolicyDocument doc;
    doc.document_id = "pull-integ";
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
    if (!fields.empty()) {
        route.field_filter.mode = TqFieldFilterMode::Allowlist;
        route.field_filter.fields = fields;
    }
    doc.routes.push_back(route);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

std::string canonical_hash_for_yaml(const std::string& yaml) {
    return "sha256:" + compute_canonical_yaml_sha256_hex(yaml);
}

bool store_version(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                   const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    if (!emit.ok) {
        return false;
    }

    PolicyVersionRecord version;
    version.generation = generation;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    version.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    return store.store_policy_version(key, version, emit.yaml).ok;
}

bool promote_active(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                    const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    if (!emit.ok) {
        return false;
    }

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
    if (!active_res.ok && generation > 1) {
        return false;
    }

    ExpectedActivePolicy expected;
    if (active_res.ok) {
        expected.generation = active_res.pointer.generation;
        expected.policy_id = active_res.pointer.policy_id;
    } else {
        expected.generation = 0;
    }
    return store.compare_and_promote_active(key, expected, pointer).ok;
}

} // namespace

TEST(RuntimePolicyPullLoopIntegrationTest, ControlPlaneClientReadsCommittedActivePointer) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    ASSERT_TRUE(store_version(store, key, gen1, 1));
    ASSERT_TRUE(promote_active(store, key, gen1, 1));

    ControlPlaneServiceConfig cp_config{};
    cp_config.policy_state_store = &store;
    ControlPlaneService cp_service(cp_config);
    InProcessControlPlanePolicyClient client(&cp_service);

    const ActivePolicyPointerResult active = client.get_active_policy(key);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.active.generation, 1u);
    EXPECT_EQ(active.active.policy_id, gen1.policy_id);

    const CanonicalPolicyResult version = client.get_policy_version(key, 1);
    ASSERT_TRUE(version.ok);
    EXPECT_FALSE(version.canonical_yaml.empty());

    destroy_db(db_path);
}

TEST(RuntimePolicyPullLoopIntegrationTest, ConvergesToCommittedGenerationTwo) {
    const std::string db_path = make_temp_db_path();
    const std::string mirror_dir = db_path + "_mirror";
    destroy_db(db_path);
    fs::remove_all(mirror_dir);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    ASSERT_TRUE(store_version(store, key, gen1, 1));
    ASSERT_TRUE(promote_active(store, key, gen1, 1));

    RuntimePolicyStore runtime_store;
    auto snap1 = build_runtime_policy_snapshot_from_ir(gen1, 1);
    ASSERT_TRUE(snap1.ok);
    ASSERT_TRUE(runtime_store.install_initial(snap1.snapshot, nullptr));

    ControlPlaneServiceConfig cp_config{};
    cp_config.policy_state_store = &store;
    ControlPlaneService cp_service(cp_config);
    InProcessControlPlanePolicyClient client(&cp_service);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2);
    ASSERT_TRUE(store_version(store, key, gen2, 2));
    ASSERT_TRUE(promote_active(store, key, gen2, 2));

    RuntimePolicyPullLoopConfig pull_config{};
    pull_config.pull.enabled = true;
    pull_config.pull.resource_key = key;
    pull_config.pull.local_mirror.enabled = true;
    pull_config.pull.local_mirror.state_dir = mirror_dir;
    pull_config.client = &client;
    pull_config.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.policy_state_store = &store;
    pull_config.activation_barrier.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.resource_key = key;

    RuntimePolicyPullLoop loop(pull_config);
    loop.tick();

    const auto active = runtime_store.load();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->generation, 2u);
    EXPECT_TRUE(fs::exists(fs::path(mirror_dir) / "active-policy.yaml"));

    destroy_db(db_path);
    fs::remove_all(mirror_dir);
}

TEST(RuntimePolicyPullLoopIntegrationTest, RestartLoadsMirrorThenConvergesToGenerationThree) {
    const std::string db_path = make_temp_db_path();
    const std::string mirror_dir = db_path + "_mirror";
    destroy_db(db_path);
    fs::remove_all(mirror_dir);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    ASSERT_TRUE(store_version(store, key, gen1, 1));
    ASSERT_TRUE(promote_active(store, key, gen1, 1));

    RuntimePolicyStore runtime_store;
    auto snap1 = build_runtime_policy_snapshot_from_ir(gen1, 1);
    ASSERT_TRUE(snap1.ok);
    ASSERT_TRUE(runtime_store.install_initial(snap1.snapshot, nullptr));

    ControlPlaneServiceConfig cp_config{};
    cp_config.policy_state_store = &store;
    ControlPlaneService cp_service(cp_config);
    InProcessControlPlanePolicyClient client(&cp_service);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2);
    ASSERT_TRUE(store_version(store, key, gen2, 2));
    ASSERT_TRUE(promote_active(store, key, gen2, 2));

    RuntimePolicyPullLoopConfig pull_config{};
    pull_config.pull.enabled = true;
    pull_config.pull.resource_key = key;
    pull_config.pull.local_mirror.enabled = true;
    pull_config.pull.local_mirror.state_dir = mirror_dir;
    pull_config.client = &client;
    pull_config.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.policy_state_store = &store;
    pull_config.activation_barrier.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.resource_key = key;

    RuntimePolicyPullLoop loop(pull_config);
    loop.tick();
    ASSERT_EQ(runtime_store.load()->generation, 2u);
    ASSERT_TRUE(fs::exists(fs::path(mirror_dir) / "active-policy.yaml"));
    ASSERT_TRUE(fs::exists(fs::path(mirror_dir) / "active-policy.meta.json"));

    RuntimePolicyStore restarted_store;
    const auto mirror_load = load_persisted_active_policy(pull_config.pull.local_mirror);
    ASSERT_TRUE(mirror_load.ok);
    EXPECT_EQ(mirror_load.metadata.generation, 2u);
    auto restarted_snap = build_runtime_policy_snapshot_from_ir(mirror_load.document, 2);
    ASSERT_TRUE(restarted_snap.ok);
    ASSERT_TRUE(restarted_store.install_initial(restarted_snap.snapshot, nullptr));
    EXPECT_EQ(restarted_store.load()->generation, 2u);
    EXPECT_EQ(restarted_store.load()->policy_identity, gen2.policy_id);

    const TqPolicyDocument gen3 = make_policy_doc("route-a", 3, { "id", "name" });
    ASSERT_TRUE(store_version(store, key, gen3, 3));
    ASSERT_TRUE(promote_active(store, key, gen3, 3));

    RouteCacheEpochStore restart_epoch_store{};
    route_cache_epoch_register(&restart_epoch_store, "route-a");

    pull_config.runtime_policy_store = &restarted_store;
    pull_config.activation_barrier.runtime_policy_store = &restarted_store;
    pull_config.activation_barrier.route_cache_epoch_store = &restart_epoch_store;
    RuntimePolicyPullLoop restart_loop(pull_config);
    restart_loop.tick();
    ASSERT_EQ(restarted_store.load()->generation, 3u);
    EXPECT_EQ(restarted_store.load()->policy_identity, gen3.policy_id);

    const auto mirror_after_gen3 = load_persisted_active_policy(pull_config.pull.local_mirror);
    ASSERT_TRUE(mirror_after_gen3.ok);
    EXPECT_EQ(mirror_after_gen3.metadata.generation, 3u);

    destroy_db(db_path);
    fs::remove_all(mirror_dir);
}

TEST(RuntimePolicyPullLoopIntegrationTest, ControlPlaneDownAfterActivationContinuesServing) {
    const std::string db_path = make_temp_db_path();
    const std::string mirror_dir = db_path + "_mirror";
    destroy_db(db_path);
    fs::remove_all(mirror_dir);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    ASSERT_TRUE(store_version(store, key, gen1, 1));
    ASSERT_TRUE(promote_active(store, key, gen1, 1));

    RuntimePolicyStore runtime_store;
    ASSERT_TRUE(runtime_store.install_initial(
        build_runtime_policy_snapshot_from_ir(gen1, 1).snapshot, nullptr));

    ControlPlaneServiceConfig cp_config{};
    cp_config.policy_state_store = &store;
    ControlPlaneService cp_service(cp_config);
    InProcessControlPlanePolicyClient client(&cp_service);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2);
    ASSERT_TRUE(store_version(store, key, gen2, 2));
    ASSERT_TRUE(promote_active(store, key, gen2, 2));

    RuntimePolicyPullLoopConfig pull_config{};
    pull_config.pull.enabled = true;
    pull_config.pull.resource_key = key;
    pull_config.pull.local_mirror.enabled = true;
    pull_config.pull.local_mirror.state_dir = mirror_dir;
    pull_config.client = &client;
    pull_config.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.policy_state_store = &store;
    pull_config.activation_barrier.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.resource_key = key;

    RuntimePolicyPullLoop loop(pull_config);
    loop.tick();
    ASSERT_EQ(runtime_store.load()->generation, 2u);
    const std::string active_policy_id_after_activation = runtime_store.load()->policy_identity;
    ASSERT_TRUE(fs::exists(fs::path(mirror_dir) / "active-policy.yaml"));
    const auto mirror_after_activation =
        load_persisted_active_policy(pull_config.pull.local_mirror);
    ASSERT_TRUE(mirror_after_activation.ok);
    EXPECT_EQ(mirror_after_activation.metadata.generation, 2u);

    InProcessControlPlanePolicyClient unavailable_client(nullptr);
    pull_config.client = &unavailable_client;
    RuntimePolicyPullLoop degraded_loop(pull_config);
    degraded_loop.tick();

    EXPECT_EQ(runtime_store.load()->generation, 2u);
    EXPECT_EQ(runtime_store.load()->policy_identity, active_policy_id_after_activation);
    EXPECT_FALSE(degraded_loop.status().control_plane_reachable);
    EXPECT_EQ(degraded_loop.status().state,
              RuntimePolicyPullState::DegradedControlPlaneUnavailable);

    degraded_loop.tick();
    EXPECT_EQ(runtime_store.load()->generation, 2u);
    EXPECT_EQ(runtime_store.load()->policy_identity, active_policy_id_after_activation);

    destroy_db(db_path);
    fs::remove_all(mirror_dir);
}

TEST(RuntimePolicyPullLoopIntegrationTest, CorruptedPolicyPointerRefusesActivation) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    ASSERT_TRUE(store_version(store, key, gen1, 1));
    ASSERT_TRUE(promote_active(store, key, gen1, 1));

    RuntimePolicyStore runtime_store;
    ASSERT_TRUE(runtime_store.install_initial(
        build_runtime_policy_snapshot_from_ir(gen1, 1).snapshot, nullptr));

    ControlPlaneServiceConfig cp_config{};
    cp_config.policy_state_store = &store;
    ControlPlaneService cp_service(cp_config);
    InProcessControlPlanePolicyClient client(&cp_service);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2);
    ASSERT_TRUE(store_version(store, key, gen2, 2));

    PolicyIrYamlEmitResult emit2 = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit2.ok);

    ActivePolicyPointer corrupted_pointer;
    corrupted_pointer.generation = 2;
    corrupted_pointer.policy_id = gen2.policy_id;
    corrupted_pointer.canonical_hash = "sha256:deadbeefdeadbeef";
    corrupted_pointer.version_key = make_version_key(key, 2);
    corrupted_pointer.yaml_key = make_yaml_key(key, 2);
    corrupted_pointer.schema_version = 1;
    corrupted_pointer.api_version = gen2.api_version;
    corrupted_pointer.kind = gen2.kind;
    corrupted_pointer.source_type = "taperql-apply";

    const auto active_res = store.load_active_pointer(key);
    ASSERT_TRUE(active_res.ok);
    ExpectedActivePolicy expected;
    expected.generation = active_res.pointer.generation;
    expected.policy_id = active_res.pointer.policy_id;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, corrupted_pointer).ok);

    RuntimePolicyPullLoopConfig pull_config{};
    pull_config.pull.enabled = true;
    pull_config.pull.resource_key = key;
    pull_config.client = &client;
    pull_config.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.policy_state_store = &store;
    pull_config.activation_barrier.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.resource_key = key;

    RuntimePolicyPullLoop loop(pull_config);
    loop.tick();

    EXPECT_EQ(runtime_store.load()->generation, 1u);
    EXPECT_EQ(runtime_store.load()->policy_identity, gen1.policy_id);
    EXPECT_EQ(loop.status().last_error_code, kErrPolicyCanonicalHashMismatch);
    EXPECT_EQ(loop.status().state, RuntimePolicyPullState::ActivationFailed);
    EXPECT_FALSE(loop.status().last_control_plane_error.empty());

    destroy_db(db_path);
}

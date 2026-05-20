// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_config.h"
#include "control_plane/control_plane_service.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "control_plane/runtime_status_report.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/committed_policy_mirror_sync.h"
#include "runtime_policy/control_plane_policy_client.h"
#include "runtime_policy/runtime_local_policy_fetch.h"
#include "runtime_policy/runtime_policy_pull_loop.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::runtime;
using namespace bytetaper::runtime_policy;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_manual_integ_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation) {
    TqPolicyDocument doc;
    doc.document_id = "manual-integ";
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
    doc.routes.push_back(route);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

std::string canonical_hash_for_yaml(const std::string& yaml) {
    return "sha256:" + compute_canonical_yaml_sha256_hex(yaml);
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
    version.source_type = "taperql-apply";
    version.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);
}

void promote_generation(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
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

    ExpectedActivePolicy expected;
    const auto active_res = store.load_active_pointer(key);
    if (active_res.ok) {
        expected.generation = active_res.pointer.generation;
        expected.policy_id = active_res.pointer.policy_id;
    } else {
        expected.generation = 0;
    }
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

RuntimeStatusReport make_runtime_report(const std::string& runtime_id, std::uint64_t generation,
                                        const std::string& policy_id,
                                        const std::string& canonical_hash) {
    RuntimeStatusReport report{};
    report.runtime_id = runtime_id;
    report.resource_key = PolicyResourceKey::default_runtime().to_string();
    report.gateway_adapter = "envoy";
    report.active_generation = generation;
    report.active_policy_id = policy_id;
    report.active_canonical_hash = canonical_hash;
    report.activation_status = "active";
    report.data_path_mode = "normal";
    report.control_plane_reachable = true;
    report.last_pull_at_unix_epoch_ms = 1'000'000;
    report.received_at_unix_epoch_ms = 1'000'000;
    return report;
}

struct IntegrationHarness {
    std::string db_path;
    std::string mirror_dir;
    RocksDBPolicyStateStore store;
    PolicyResourceKey key = PolicyResourceKey::default_runtime();
    RuntimePolicyStore runtime_store;
    ControlPlaneServiceConfig cp_config{};
    std::unique_ptr<ControlPlaneService> cp_service;
    std::unique_ptr<InProcessControlPlanePolicyClient> client;
    LocalPolicyPersistenceConfig mirror{};

    explicit IntegrationHarness(const std::string& path, const std::string& mirror_path)
        : db_path(path), mirror_dir(mirror_path), store(path.c_str()) {}

    void init_service() {
        cp_config.policy_state_store = &store;
        mirror.enabled = true;
        mirror.state_dir = mirror_dir;
        cp_config.repair_local_hook = [this](const PolicyRepairLocalRequest&,
                                             const ActivePolicyPointer&) {
            RepairLocalHookResult hook{};
            const auto sync = sync_local_mirror_from_committed(client.get(), key, mirror, 0);
            hook.ok = sync.ok;
            hook.error = sync.error;
            hook.error_code = sync.error_code;
            return hook;
        };
        cp_config.adopt_local_fetch_hook = [this](const PolicyAdoptLocalRequest&,
                                                  const RuntimeStatusReport& runtime_report) {
            return fetch_local_policy_from_mirror(mirror, runtime_report);
        };
        cp_service = std::make_unique<ControlPlaneService>(cp_config);
        client = std::make_unique<InProcessControlPlanePolicyClient>(cp_service.get());
    }
};

} // namespace

TEST(ManualResolutionIntegrationTest, RollbackPullLoopConverges) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument gen2 = make_policy_doc("route-b", 2);
    const TqPolicyDocument gen3 = make_policy_doc("route-c", 3);
    seed_generation(store, key, gen1, 1);
    promote_generation(store, key, gen1, 1);
    seed_generation(store, key, gen2, 2);
    promote_generation(store, key, gen2, 2);
    seed_generation(store, key, gen3, 3);
    promote_generation(store, key, gen3, 3);

    RuntimePolicyStore runtime_store;
    ASSERT_TRUE(runtime_store.install_initial(
        build_runtime_policy_snapshot_from_ir(gen3, 3).snapshot, nullptr));

    ControlPlaneServiceConfig cp_config{};
    cp_config.policy_state_store = &store;
    ControlPlaneService cp_service(cp_config);
    InProcessControlPlanePolicyClient client(&cp_service);

    PolicyIrYamlEmitResult emit3 = emit_policy_ir_canonical_yaml(gen3);
    ASSERT_TRUE(emit3.ok);

    ASSERT_TRUE(
        cp_service
            .report_runtime_status(make_runtime_report("bytetaper-runtime", 3, gen3.policy_id,
                                                       canonical_hash_for_yaml(emit3.yaml)))
            .ok);

    PolicyRollbackRequest rollback{};
    rollback.resource_key = key;
    rollback.target_generation = 1;
    rollback.expected_current_generation = 3;
    rollback.expected_current_policy_id = gen3.policy_id;
    rollback.request_id = "integ-rollback";
    rollback.confirm = true;
    const PolicyRollbackResult rollback_result = cp_service.rollback(rollback);
    ASSERT_TRUE(rollback_result.ok) << rollback_result.error;
    EXPECT_EQ(rollback_result.new_generation, 4u);

    RuntimePolicyPullLoopConfig pull_config{};
    pull_config.pull.enabled = true;
    pull_config.pull.resource_key = key;
    pull_config.client = &client;
    pull_config.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.policy_state_store = &store;
    pull_config.activation_barrier.runtime_policy_store = &runtime_store;
    pull_config.activation_barrier.resource_key = key;

    const auto active_after_rollback = store.load_active_pointer(key);
    ASSERT_TRUE(active_after_rollback.ok);
    ASSERT_EQ(active_after_rollback.pointer.generation, 4u);

    RuntimePolicyPullLoop loop(pull_config);
    loop.tick();

    const auto active_snapshot = runtime_store.load();
    ASSERT_NE(active_snapshot, nullptr);
    const auto pull_status = loop.status();
    EXPECT_EQ(active_snapshot->generation, 4u) << "pull_error=" << pull_status.last_error_code
                                               << " msg=" << pull_status.last_control_plane_error;

    const auto gen1_yaml = store.load_policy_version(key, 1);
    const auto gen4_yaml = store.load_policy_version(key, 4);
    ASSERT_TRUE(gen1_yaml.ok);
    ASSERT_TRUE(gen4_yaml.ok);
    EXPECT_EQ(gen4_yaml.record.policy_id, gen1_yaml.record.policy_id);
    EXPECT_EQ(gen4_yaml.record.canonical_hash, gen1_yaml.record.canonical_hash);
    EXPECT_EQ(gen4_yaml.canonical_yaml, gen1_yaml.canonical_yaml);

    destroy_db(db_path);
}

TEST(ManualResolutionIntegrationTest, RepairLocalRestoresTamperedMirror) {
    const std::string db_path = make_temp_db_path();
    const std::string mirror_dir =
        (fs::temp_directory_path() / ("bt_manual_mirror_" + db_path)).string();
    destroy_db(db_path);
    fs::remove_all(mirror_dir);

    IntegrationHarness harness(db_path, mirror_dir);
    ASSERT_TRUE(harness.store.is_open());
    harness.init_service();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_generation(harness.store, harness.key, gen1, 1);
    promote_generation(harness.store, harness.key, gen1, 1);
    PolicyIrYamlEmitResult emit1 = emit_policy_ir_canonical_yaml(gen1);
    ASSERT_TRUE(emit1.ok);
    const std::string gen1_hash = canonical_hash_for_yaml(emit1.yaml);

    ASSERT_TRUE(harness.runtime_store.install_initial(
        build_runtime_policy_snapshot_from_ir(gen1, 1).snapshot, nullptr));

    ASSERT_TRUE(harness.cp_service
                    ->report_runtime_status(
                        make_runtime_report("bytetaper-runtime", 1, gen1.policy_id, gen1_hash))
                    .ok);

    const auto initial_sync =
        sync_local_mirror_from_committed(harness.client.get(), harness.key, harness.mirror, 1);
    ASSERT_TRUE(initial_sync.ok) << initial_sync.error;

    const fs::path policy_file = fs::path(harness.mirror_dir) / "active-policy.yaml";
    ASSERT_TRUE(fs::exists(policy_file));
    {
        std::ofstream out(policy_file, std::ios::trunc);
        out << "corrupted: true\n";
    }

    PolicyRepairLocalRequest repair{};
    repair.resource_key = harness.key;
    repair.runtime_id = "bytetaper-runtime";
    repair.expected_committed_generation = 1;
    repair.expected_committed_policy_id = gen1.policy_id;
    repair.request_id = "integ-repair";
    repair.confirm = true;
    const PolicyRepairLocalResult repair_result = harness.cp_service->repair_local(repair);
    ASSERT_TRUE(repair_result.ok) << repair_result.error;

    const auto recovered = load_persisted_active_policy(harness.mirror);
    ASSERT_TRUE(recovered.ok);
    EXPECT_EQ(recovered.metadata.generation, 1u);
    EXPECT_EQ(recovered.metadata.policy_identity, gen1.policy_id);

    const auto active = harness.store.load_active_pointer(harness.key);
    ASSERT_TRUE(active.ok);
    EXPECT_EQ(active.pointer.generation, 1u);

    destroy_db(db_path);
    fs::remove_all(mirror_dir);
}

TEST(ManualResolutionIntegrationTest, AdoptDivergentLocalPolicyFetchedFromRuntimeMirror) {
    const std::string db_path = make_temp_db_path();
    const std::string mirror_dir =
        (fs::temp_directory_path() / ("bt_manual_mirror_" + db_path)).string();
    destroy_db(db_path);
    fs::remove_all(mirror_dir);

    IntegrationHarness harness(db_path, mirror_dir);
    ASSERT_TRUE(harness.store.is_open());
    harness.init_service();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument divergent = make_policy_doc("route-divergent", 2);
    seed_generation(harness.store, harness.key, gen1, 1);
    promote_generation(harness.store, harness.key, gen1, 1);

    ASSERT_TRUE(harness.runtime_store.install_initial(
        build_runtime_policy_snapshot_from_ir(gen1, 1).snapshot, nullptr));

    PersistedPolicyMetadata mirror_meta{};
    mirror_meta.policy_identity = divergent.policy_id;
    mirror_meta.generation = 2;
    mirror_meta.resource_key = harness.key.to_string();
    mirror_meta.metadata_schema_version = 1;
    mirror_meta.schema_version = 1;
    mirror_meta.api_version = divergent.api_version;
    mirror_meta.kind = divergent.kind;
    mirror_meta.source_type = "manual-adopt";
    mirror_meta.active_policy_file = harness.mirror.active_policy_filename;
    mirror_meta.written_at_unix_epoch_ms = 1'000'000;
    ASSERT_TRUE(persist_active_policy_canonical_yaml(harness.mirror, divergent, mirror_meta).ok);

    const auto mirror_loaded = load_persisted_active_policy(harness.mirror);
    ASSERT_TRUE(mirror_loaded.ok) << mirror_loaded.error;
    const std::string mirror_hash = mirror_loaded.metadata.canonical_hash;

    ASSERT_TRUE(harness.cp_service
                    ->report_runtime_status(make_runtime_report("bytetaper-runtime", 2,
                                                                divergent.policy_id, mirror_hash))
                    .ok);

    PolicyAdoptLocalRequest adopt{};
    adopt.resource_key = harness.key;
    adopt.runtime_id = "bytetaper-runtime";
    adopt.expected_current_generation = 1;
    adopt.local_generation = 2;
    adopt.local_policy_id = divergent.policy_id;
    adopt.request_id = "integ-adopt";
    adopt.confirm_divergent_adoption = true;
    const PolicyAdoptLocalResult adopt_result = harness.cp_service->adopt_local(adopt);
    ASSERT_TRUE(adopt_result.ok) << adopt_result.error;
    EXPECT_EQ(adopt_result.new_generation, 2u);
    EXPECT_EQ(adopt_result.new_policy_id, divergent.policy_id);

    const auto committed = harness.store.load_active_pointer(harness.key);
    ASSERT_TRUE(committed.ok);
    EXPECT_EQ(committed.pointer.generation, 2u);
    EXPECT_EQ(committed.pointer.policy_id, divergent.policy_id);

    RuntimePolicyPullLoopConfig pull_config{};
    pull_config.pull.enabled = true;
    pull_config.pull.resource_key = harness.key;
    pull_config.client = harness.client.get();
    pull_config.runtime_policy_store = &harness.runtime_store;
    pull_config.activation_barrier.policy_state_store = &harness.store;
    pull_config.activation_barrier.runtime_policy_store = &harness.runtime_store;
    pull_config.activation_barrier.resource_key = harness.key;

    RuntimePolicyPullLoop loop(pull_config);
    loop.tick();

    const auto active_snapshot = harness.runtime_store.load();
    ASSERT_NE(active_snapshot, nullptr);
    EXPECT_EQ(active_snapshot->generation, 2u);
    EXPECT_EQ(active_snapshot->policy_identity, divergent.policy_id);

    destroy_db(db_path);
    fs::remove_all(mirror_dir);
}

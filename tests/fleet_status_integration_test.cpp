// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_service.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "control_plane/runtime_status_report.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_fleet_integ_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation) {
    TqPolicyDocument doc;
    doc.document_id = "fleet-integ";
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

void seed_active_policy(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
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

    ActivePolicyPointer pointer;
    pointer.generation = generation;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = version.canonical_hash;
    pointer.version_key = make_version_key(key, generation);
    pointer.yaml_key = make_yaml_key(key, generation);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    expected.generation = generation > 1 ? generation - 1 : 0;
    if (generation > 1) {
        const auto active = store.load_active_pointer(key);
        if (active.ok) {
            expected.generation = active.pointer.generation;
            expected.policy_id = active.pointer.policy_id;
        }
    }
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

RuntimeStatusReport make_runtime_report(const std::string& runtime_id, std::uint64_t generation,
                                        const std::string& policy_id,
                                        const std::string& canonical_hash, std::int64_t now_ms) {
    RuntimeStatusReport report{};
    report.runtime_id = runtime_id;
    report.resource_key = PolicyResourceKey::default_runtime().to_string();
    report.gateway_adapter = "envoy";
    report.active_generation = generation;
    report.active_policy_id = policy_id;
    report.active_canonical_hash = canonical_hash;
    report.activation_status = "active";
    report.control_plane_reachable = true;
    report.data_path_mode = "optimized";
    report.last_pull_at_unix_epoch_ms = now_ms;
    report.last_activated_at_unix_epoch_ms = now_ms;
    report.received_at_unix_epoch_ms = now_ms;
    return report;
}

} // namespace

TEST(FleetStatusIntegrationTest, TwoRuntimesConvergeAfterStaleReport) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);

    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(store, key, gen1, 1);

    const TqPolicyDocument gen2 = make_policy_doc("route-a", 2);
    seed_active_policy(store, key, gen2, 2);

    PolicyIrYamlEmitResult emit1 = emit_policy_ir_canonical_yaml(gen1);
    PolicyIrYamlEmitResult emit2 = emit_policy_ir_canonical_yaml(gen2);
    ASSERT_TRUE(emit1.ok);
    ASSERT_TRUE(emit2.ok);

    ControlPlaneServiceConfig config{};
    config.policy_state_store = &store;
    ControlPlaneService service(config);

    const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    ASSERT_TRUE(service
                    .report_runtime_status(make_runtime_report(
                        "runtime-a", 2, gen2.policy_id, canonical_hash_for_yaml(emit2.yaml), now))
                    .ok);
    ASSERT_TRUE(service
                    .report_runtime_status(make_runtime_report(
                        "runtime-b", 1, gen1.policy_id, canonical_hash_for_yaml(emit1.yaml), now))
                    .ok);

    const FleetStatusResult fleet_stale = service.get_fleet_status(key);
    ASSERT_TRUE(fleet_stale.ok);
    EXPECT_FALSE(fleet_stale.status.fleet.converged);
    EXPECT_GE(fleet_stale.status.fleet.stale_count, 1u);
    EXPECT_EQ(fleet_stale.status.committed.generation, 2u);

    ASSERT_TRUE(service
                    .report_runtime_status(make_runtime_report(
                        "runtime-b", 2, gen2.policy_id, canonical_hash_for_yaml(emit2.yaml), now))
                    .ok);

    const FleetStatusResult fleet_converged = service.get_fleet_status(key);
    ASSERT_TRUE(fleet_converged.ok);
    EXPECT_TRUE(fleet_converged.status.fleet.converged);
    EXPECT_EQ(fleet_converged.status.fleet.converged_count, 2u);
    EXPECT_EQ(fleet_converged.status.fleet.runtime_count, 2u);

    destroy_db(db_path);
}

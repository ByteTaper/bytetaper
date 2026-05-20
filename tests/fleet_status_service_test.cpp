// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_metrics.h"
#include "control_plane/fleet_status_service.h"
#include "control_plane/rocksdb_policy_state_store.h"

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_fleet_test_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

std::int64_t now_unix_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

RuntimeStatusReport make_report(const std::string& runtime_id, const std::string& resource_key,
                                std::uint64_t generation, const std::string& policy_id,
                                const std::string& hash, std::int64_t received_at_ms) {
    RuntimeStatusReport report{};
    report.runtime_id = runtime_id;
    report.resource_key = resource_key;
    report.gateway_adapter = "envoy";
    report.active_generation = generation;
    report.active_policy_id = policy_id;
    report.active_canonical_hash = hash;
    report.activation_status = "active";
    report.control_plane_reachable = true;
    report.data_path_mode = "optimized";
    report.last_pull_at_unix_epoch_ms = received_at_ms;
    report.last_activated_at_unix_epoch_ms = received_at_ms;
    report.received_at_unix_epoch_ms = received_at_ms;
    return report;
}

} // namespace

TEST(FleetStatusServiceTest, IngestRejectsInvalidReport) {
    FleetStatusService service(FleetStatusConfig{}, nullptr);
    RuntimeStatusReport report{};
    report.resource_key = "policy/default/runtime";
    const auto result = service.ingest(report, 1'000'000);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrRuntimeStatusInvalid);
}

TEST(FleetStatusServiceTest, MultipleResourceKeysDoNotMix) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey default_key = PolicyResourceKey::default_runtime();

    PolicyVersionRecord version{};
    version.generation = 1;
    version.policy_id = "policy-default";
    version.canonical_hash = "sha256:default";
    ASSERT_TRUE(store.store_policy_version(default_key, version, "routes: []\n").ok);

    ActivePolicyPointer pointer{};
    pointer.generation = 1;
    pointer.policy_id = "policy-default";
    pointer.canonical_hash = "sha256:default";
    pointer.resource_key = default_key.to_string();
    pointer.version_key = make_version_key(default_key, 1);
    pointer.yaml_key = make_yaml_key(default_key, 1);
    ExpectedActivePolicy expected{};
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(default_key, expected, pointer).ok);

    FleetStatusService service(FleetStatusConfig{}, &store);
    const std::int64_t now = now_unix_epoch_ms();
    ASSERT_TRUE(service
                    .ingest(make_report("runtime-a", default_key.to_string(), 1, "policy-default",
                                        "sha256:default", now),
                            now)
                    .ok);
    ASSERT_TRUE(service
                    .ingest(make_report("runtime-b", "policy/other/runtime", 1, "policy-other",
                                        "sha256:other", now),
                            now)
                    .ok);

    const FleetStatusResult fleet = service.get_fleet_status(default_key, now);
    ASSERT_TRUE(fleet.ok);
    ASSERT_EQ(fleet.status.runtimes.size(), 1u);
    EXPECT_EQ(fleet.status.runtimes[0].runtime_id, "runtime-a");

    destroy_db(db_path);
}

TEST(FleetStatusServiceTest, GetFleetStatusWithoutActivePointerFails) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    FleetStatusService service(FleetStatusConfig{}, &store);
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();
    const auto result = service.get_fleet_status(key, 1'000'000);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kErrFleetStatusActivePointerMissing);

    destroy_db(db_path);
}

TEST(FleetStatusServiceTest, FleetSummaryCountsAreCorrect) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    PolicyVersionRecord version{};
    version.generation = 2;
    version.policy_id = "policy-2";
    version.canonical_hash = "sha256:two";
    ASSERT_TRUE(store.store_policy_version(key, version, "routes: []\n").ok);

    ActivePolicyPointer pointer{};
    pointer.generation = 2;
    pointer.policy_id = "policy-2";
    pointer.canonical_hash = "sha256:two";
    pointer.resource_key = key.to_string();
    pointer.version_key = make_version_key(key, 2);
    pointer.yaml_key = make_yaml_key(key, 2);
    ExpectedActivePolicy expected{};
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);

    FleetStatusConfig config{};
    config.runtime_heartbeat_timeout_ms = 60'000;
    config.runtime_status_retention_ms = 3'600'000;
    FleetStatusService service(config, &store);

    const std::int64_t now = now_unix_epoch_ms();
    ASSERT_TRUE(
        service
            .ingest(make_report("runtime-a", key.to_string(), 2, "policy-2", "sha256:two", now),
                    now)
            .ok);
    ASSERT_TRUE(
        service
            .ingest(make_report("runtime-b", key.to_string(), 1, "policy-1", "sha256:one", now),
                    now)
            .ok);
    ASSERT_TRUE(
        service
            .ingest(make_report("runtime-c", key.to_string(), 2, "policy-2", "sha256:bad", now),
                    now)
            .ok);

    const FleetStatusResult fleet = service.get_fleet_status(key, now);
    ASSERT_TRUE(fleet.ok) << fleet.error;
    EXPECT_EQ(fleet.status.fleet.runtime_count, 3u);
    EXPECT_EQ(fleet.status.fleet.converged_count, 1u);
    EXPECT_EQ(fleet.status.fleet.stale_count, 1u);
    EXPECT_EQ(fleet.status.fleet.divergent_count, 1u);
    EXPECT_FALSE(fleet.status.fleet.converged);

    for (const RuntimeFleetEntry& entry : fleet.status.runtimes) {
        if (entry.runtime_id == "runtime-b") {
            EXPECT_EQ(entry.convergence_error_code, kErrRuntimeStatusStale);
        }
        if (entry.runtime_id == "runtime-c") {
            EXPECT_EQ(entry.convergence_error_code, kErrRuntimeStatusPolicyMismatch);
        }
    }

    ControlPlaneMetrics metrics{};
    update_fleet_metrics(&metrics, fleet.status.fleet.runtime_count,
                         fleet.status.fleet.converged_count, fleet.status.fleet.stale_count,
                         fleet.status.fleet.failed_count, fleet.status.fleet.unreachable_count,
                         fleet.status.fleet.degraded_count, fleet.status.fleet.converged);
    EXPECT_EQ(metrics.fleet_stale_count.load(), 1u);
    EXPECT_EQ(metrics.fleet_runtime_count.load(), 3u);

    destroy_db(db_path);
}

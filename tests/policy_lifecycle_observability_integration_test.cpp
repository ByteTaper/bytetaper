// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/audit_retention_config.h"
#include "control_plane/control_plane_metrics.h"
#include "control_plane/fleet_status_config.h"
#include "control_plane/fleet_status_service.h"
#include "control_plane/policy_apply_transaction.h"
#include "control_plane/policy_lifecycle_emitter.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "operational/policy_activation_barrier.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/runtime_policy_metrics.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::operational;
using namespace bytetaper::runtime_policy;
using namespace bytetaper::taperquery;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_lifecycle_integ_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation,
                                 const std::string& expected_base_sha = "") {
    TqPolicyDocument doc;
    doc.document_id = "lifecycle-integ";
    doc.source_name = "test";
    doc.generation = generation;
    doc.expected_base_sha = expected_base_sha;
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

    ExpectedActivePolicy expected{};
    expected.generation = 0;
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

class AuditCapturingPolicyStateStore : public PolicyStateStore {
public:
    explicit AuditCapturingPolicyStateStore(RocksDBPolicyStateStore* inner) : inner_(inner) {}

    StorePolicyVersionResult store_policy_version(const PolicyResourceKey& key,
                                                  const PolicyVersionRecord& version,
                                                  const std::string& canonical_yaml) override {
        return inner_->store_policy_version(key, version, canonical_yaml);
    }

    LoadPolicyVersionResult load_policy_version(const PolicyResourceKey& key,
                                                std::uint64_t generation) override {
        return inner_->load_policy_version(key, generation);
    }

    LoadActivePointerResult load_active_pointer(const PolicyResourceKey& key) override {
        return inner_->load_active_pointer(key);
    }

    PromoteActiveResult compare_and_promote_active(const PolicyResourceKey& key,
                                                   const ExpectedActivePolicy& expected,
                                                   const ActivePolicyPointer& next) override {
        return inner_->compare_and_promote_active(key, expected, next);
    }

    CommitPolicyGenerationWithAuditResult commit_policy_generation_with_audit(
        const CommitPolicyGenerationWithAuditParams& params) override {
        captured_.push_back(params.audit);
        return inner_->commit_policy_generation_with_audit(params);
    }

    AppendAuditResult append_audit_record(const PolicyResourceKey& key,
                                          const PolicyAuditRecord& record) override {
        captured_.push_back(record);
        return inner_->append_audit_record(key, record);
    }

    StorePolicyUpdateJobResult store_policy_update_job(const PolicyResourceKey&,
                                                       const PolicyUpdateJobRecord&) override {
        StorePolicyUpdateJobResult res;
        res.ok = true;
        return res;
    }

    LoadPolicyUpdateJobResult load_policy_update_job(const PolicyResourceKey&,
                                                     const std::string&) override {
        LoadPolicyUpdateJobResult res;
        res.not_found = true;
        return res;
    }

    bool has_audit_event(const char* event_type) const {
        for (const PolicyAuditRecord& record : captured_) {
            if (record.event_type == event_type) {
                return true;
            }
        }
        return false;
    }

private:
    RocksDBPolicyStateStore* inner_;
    std::vector<PolicyAuditRecord> captured_;
};

PolicyUpdateJob make_apply_job(const PolicyResourceKey& key, const TqPolicyDocument& base,
                               const std::string& candidate_yaml, const std::string& job_id) {
    PolicyUpdateJob job;
    job.job_id = job_id;
    job.resource_key = key.to_string();
    job.source_type = "yaml";
    job.expected_base_generation = 1;
    job.expected_base_policy_id = base.policy_id;
    job.apply_request.source = candidate_yaml;
    job.apply_request.mode = TqApplyMode::Apply;
    job.apply_request.expected_base_identity = base.policy_id;
    job.apply_request.strict_production = true;
    return job;
}

} // namespace

TEST(PolicyLifecycleObservabilityIntegrationTest, ApplySuccessWritesAuditAndPromoteMetric) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    TqPolicyDocument base = make_policy_doc("route-base", 1);
    seed_active_policy(store, key, base, 1);

    TqPolicyDocument candidate = make_policy_doc("route-candidate", 2, base.policy_id);
    PolicyIrYamlEmitResult candidate_emit = emit_policy_ir_canonical_yaml(candidate);
    ASSERT_TRUE(candidate_emit.ok);

    AuditCapturingPolicyStateStore audit_store(&store);
    ControlPlaneMetrics cp_metrics{};
    PolicyLifecycleEmitterConfig emitter_config{};
    emitter_config.policy_state_store = &audit_store;
    emitter_config.control_plane_metrics = &cp_metrics;
    emitter_config.audit_retention.enabled = true;
    PolicyLifecycleEmitter emitter(emitter_config);

    PolicyApplyTransactionConfig tx_config{};
    tx_config.policy_state_store = &audit_store;
    tx_config.resource_key = key;
    tx_config.lifecycle_emitter = &emitter;
    tx_config.control_plane_metrics = &cp_metrics;

    PolicyUpdateJob job = make_apply_job(key, base, candidate_emit.yaml, "job-lifecycle-integ");
    PolicyApplyTransaction transaction(tx_config);
    const PolicyApplyTransactionResult result = transaction.execute(job);

    ASSERT_TRUE(result.ok) << result.error_message;
    EXPECT_TRUE(audit_store.has_audit_event("policy_apply_committed"));
    EXPECT_GE(cp_metrics.active_pointer_promote_total.load(), 1u);
    EXPECT_GE(cp_metrics.policy_apply_total.load(), 1u);

    destroy_db(db_path);
}

TEST(PolicyLifecycleObservabilityIntegrationTest, ActivationFailureIncrementsFailedMetric) {
    RuntimePolicyMetrics metrics{};
    PolicyActivationBarrierConfig barrier_config{};
    barrier_config.runtime_policy_metrics = &metrics;

    PolicyActivationRequest request{};
    request.generation = 1;
    request.policy_id = "missing-policy";

    PolicyActivationBarrier barrier(barrier_config);
    const PolicyActivationResult activation = barrier.activate(request);

    EXPECT_FALSE(activation.ok);
    EXPECT_GE(metrics.activation_failed_total.load(), 1u);
}

TEST(PolicyLifecycleObservabilityIntegrationTest, FleetStaleRuntimeUpdatesStaleGauge) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());
    const PolicyResourceKey key = PolicyResourceKey::default_runtime();

    TqPolicyDocument doc = make_policy_doc("route-fleet", 2);
    seed_active_policy(store, key, doc, 2);

    FleetStatusConfig fleet_config{};
    fleet_config.runtime_heartbeat_timeout_ms = 60'000;
    fleet_config.runtime_status_retention_ms = 3'600'000;
    FleetStatusService fleet_service(fleet_config, &store);

    const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

    RuntimeStatusReport stale_report{};
    stale_report.runtime_id = "runtime-stale";
    stale_report.resource_key = key.to_string();
    stale_report.gateway_adapter = "envoy";
    stale_report.active_generation = 1;
    stale_report.active_policy_id = "stale-policy";
    stale_report.active_canonical_hash = doc.policy_id.empty() ? "sha256:stale" : "sha256:0000";
    stale_report.activation_status = "active";
    stale_report.control_plane_reachable = true;
    stale_report.data_path_mode = "optimized";
    stale_report.last_pull_at_unix_epoch_ms = now;
    stale_report.last_activated_at_unix_epoch_ms = now;
    stale_report.received_at_unix_epoch_ms = now;
    const RuntimeStatusReportResult ingest_res = fleet_service.ingest(stale_report, now);
    ASSERT_TRUE(ingest_res.ok) << ingest_res.error;

    const FleetStatusResult fleet = fleet_service.get_fleet_status(key, now);
    ASSERT_TRUE(fleet.ok);
    EXPECT_EQ(fleet.status.fleet.stale_count, 1u);

    ControlPlaneMetrics cp_metrics{};
    update_fleet_metrics(&cp_metrics, fleet.status.fleet.runtime_count,
                         fleet.status.fleet.converged_count, fleet.status.fleet.stale_count,
                         fleet.status.fleet.failed_count, fleet.status.fleet.unreachable_count,
                         fleet.status.fleet.degraded_count, fleet.status.fleet.converged);
    EXPECT_EQ(cp_metrics.fleet_stale_count.load(), 1u);

    destroy_db(db_path);
}

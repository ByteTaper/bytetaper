// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/runtime_convergence_status.h"
#include "control_plane/runtime_status_report.h"

#include <gtest/gtest.h>

using namespace bytetaper::control_plane;

namespace {

ActivePolicyPointer make_committed(std::uint64_t generation) {
    ActivePolicyPointer committed{};
    committed.generation = generation;
    committed.policy_id = "policy-gen-" + std::to_string(generation);
    committed.canonical_hash = "sha256:committed" + std::to_string(generation);
    return committed;
}

RuntimeStatusReport make_runtime_report(const std::string& runtime_id, std::uint64_t generation,
                                        const std::string& policy_id,
                                        const std::string& canonical_hash,
                                        const std::string& activation_status,
                                        const std::string& data_path_mode = "optimized",
                                        std::int64_t received_at_ms = 1'000'000) {
    RuntimeStatusReport report{};
    report.runtime_id = runtime_id;
    report.resource_key = "policy/default/runtime";
    report.gateway_adapter = "envoy";
    report.active_generation = generation;
    report.active_policy_id = policy_id;
    report.active_canonical_hash = canonical_hash;
    report.activation_status = activation_status;
    report.control_plane_reachable = true;
    report.data_path_mode = data_path_mode;
    report.last_pull_at_unix_epoch_ms = received_at_ms;
    report.last_activated_at_unix_epoch_ms = received_at_ms;
    report.received_at_unix_epoch_ms = received_at_ms;
    return report;
}

} // namespace

TEST(RuntimeConvergenceStatusTest, ValidateRejectsMissingRuntimeId) {
    RuntimeStatusReport report = make_runtime_report("", 2, "p", "sha256:h", "active");
    report.runtime_id.clear();
    std::string error;
    EXPECT_FALSE(validate_runtime_status_report(report, &error));
    EXPECT_FALSE(error.empty());
}

TEST(RuntimeConvergenceStatusTest, MatchingGenerationPolicyHashIsConverged) {
    const ActivePolicyPointer committed = make_committed(2);
    RuntimeStatusReport report = make_runtime_report("runtime-a", 2, committed.policy_id,
                                                     committed.canonical_hash, "active");
    const FleetStatusConfig config{};
    EXPECT_EQ(classify_runtime_convergence(committed, report, 1'000'500, config),
              RuntimeConvergenceStatus::Converged);
}

TEST(RuntimeConvergenceStatusTest, LowerGenerationIsStale) {
    const ActivePolicyPointer committed = make_committed(3);
    RuntimeStatusReport report = make_runtime_report("runtime-a", 2, committed.policy_id,
                                                     committed.canonical_hash, "active");
    const FleetStatusConfig config{};
    EXPECT_EQ(classify_runtime_convergence(committed, report, 1'000'500, config),
              RuntimeConvergenceStatus::Stale);
}

TEST(RuntimeConvergenceStatusTest, SameGenerationDifferentHashIsDivergent) {
    const ActivePolicyPointer committed = make_committed(2);
    RuntimeStatusReport report =
        make_runtime_report("runtime-a", 2, committed.policy_id, "sha256:other", "active");
    const FleetStatusConfig config{};
    EXPECT_EQ(classify_runtime_convergence(committed, report, 1'000'500, config),
              RuntimeConvergenceStatus::Divergent);
}

TEST(RuntimeConvergenceStatusTest, ActivationFailedIsFailed) {
    const ActivePolicyPointer committed = make_committed(2);
    RuntimeStatusReport report = make_runtime_report("runtime-a", 2, committed.policy_id,
                                                     committed.canonical_hash, "activation_failed");
    const FleetStatusConfig config{};
    EXPECT_EQ(classify_runtime_convergence(committed, report, 1'000'500, config),
              RuntimeConvergenceStatus::Failed);
}

TEST(RuntimeConvergenceStatusTest, PassThroughDataPathIsDegraded) {
    const ActivePolicyPointer committed = make_committed(2);
    RuntimeStatusReport report = make_runtime_report(
        "runtime-a", 2, committed.policy_id, committed.canonical_hash, "active", "pass-through");
    const FleetStatusConfig config{};
    EXPECT_EQ(classify_runtime_convergence(committed, report, 1'000'500, config),
              RuntimeConvergenceStatus::Degraded);
}

TEST(RuntimeConvergenceStatusTest, OldHeartbeatIsUnreachable) {
    const ActivePolicyPointer committed = make_committed(2);
    RuntimeStatusReport report = make_runtime_report("runtime-a", 2, committed.policy_id,
                                                     committed.canonical_hash, "active");
    FleetStatusConfig config{};
    config.runtime_heartbeat_timeout_ms = 1000;
    EXPECT_EQ(classify_runtime_convergence(committed, report, 1'002'500, config),
              RuntimeConvergenceStatus::Unreachable);
}

TEST(RuntimeConvergenceStatusTest, ConvergenceStatusErrorCodesMatchSpec) {
    EXPECT_STREQ(convergence_status_error_code(RuntimeConvergenceStatus::Stale),
                 kErrRuntimeStatusStale);
    EXPECT_STREQ(convergence_status_error_code(RuntimeConvergenceStatus::Divergent),
                 kErrRuntimeStatusPolicyMismatch);
    EXPECT_STREQ(convergence_status_error_code(RuntimeConvergenceStatus::Unreachable),
                 kErrRuntimeStatusUnreachable);
    EXPECT_EQ(convergence_status_error_code(RuntimeConvergenceStatus::Converged), nullptr);
    EXPECT_EQ(convergence_status_error_code(RuntimeConvergenceStatus::Failed), nullptr);
}

TEST(RuntimeConvergenceStatusTest, CleanupPendingIsConverged) {
    const ActivePolicyPointer committed = make_committed(2);
    RuntimeStatusReport report = make_runtime_report("runtime-a", 2, committed.policy_id,
                                                     committed.canonical_hash, "cleanup_pending");
    const FleetStatusConfig config{};
    EXPECT_EQ(classify_runtime_convergence(committed, report, 1'000'500, config),
              RuntimeConvergenceStatus::Converged);
}

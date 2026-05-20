// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_metrics.h"
#include "control_plane/policy_apply_transaction.h"

#include <gtest/gtest.h>

using namespace bytetaper::control_plane;

TEST(ControlPlaneMetricsTest, ApplySuccessAndFailureCounters) {
    ControlPlaneMetrics metrics{};
    record_policy_apply_success(&metrics, 42);
    record_policy_apply_failure(&metrics, "store_version", "POLICY_APPLY_STORE_VERSION_FAILED");
    record_policy_cas_conflict(&metrics);
    record_queue_rejection(&metrics);
    record_manual_operation(&metrics, "repair-local", false);
    record_policy_update_job(&metrics, false, 10);

    EXPECT_EQ(metrics.policy_apply_total.load(), 1u);
    EXPECT_EQ(metrics.policy_apply_failed_total.load(), 1u);
    EXPECT_EQ(metrics.policy_cas_conflict_total.load(), 1u);
    EXPECT_EQ(metrics.policy_update_queue_rejected_total.load(), 1u);
    EXPECT_EQ(metrics.manual_repair_total.load(), 1u);
    EXPECT_EQ(metrics.manual_repair_failed_total.load(), 1u);
    EXPECT_EQ(metrics.policy_update_job_failed_total.load(), 1u);
}

TEST(ControlPlaneMetricsTest, FleetGaugeRefresh) {
    ControlPlaneMetrics metrics{};
    update_fleet_metrics(&metrics, 3, 1, 2, 0, 0, 0, false);
    EXPECT_EQ(metrics.fleet_runtime_count.load(), 3u);
    EXPECT_EQ(metrics.fleet_stale_count.load(), 2u);
    EXPECT_EQ(metrics.fleet_converged.load(), 0u);
}

TEST(ControlPlaneMetricsTest, ApplyStageDurationIncrementsPerStage) {
    ControlPlaneMetrics metrics{};
    record_policy_apply_stage_duration(
        &metrics, policy_apply_stage_index(PolicyApplyStage::BuildCandidate), 25);
    record_policy_apply_stage_duration(
        &metrics, policy_apply_stage_index(PolicyApplyStage::StoreVersion), 10);
    const std::size_t compile_idx = policy_apply_stage_index(PolicyApplyStage::BuildCandidate);
    const std::size_t store_idx = policy_apply_stage_index(PolicyApplyStage::StoreVersion);
    EXPECT_EQ(metrics.policy_apply_stage_duration_ms[compile_idx].count.load(), 1u);
    EXPECT_EQ(metrics.policy_apply_stage_duration_ms[compile_idx].total.load(), 25u);
    EXPECT_EQ(metrics.policy_apply_stage_duration_ms[store_idx].total.load(), 10u);
}

TEST(ControlPlaneMetricsTest, PrometheusRenderUsesPhaseDurationMetricNames) {
    ControlPlaneMetrics metrics{};
    record_policy_apply_success(&metrics, 42);
    record_policy_update_job(&metrics, true, 15);
    record_policy_apply_stage_duration(
        &metrics, policy_apply_stage_index(PolicyApplyStage::StoreVersion), 10);
    char buf[4096];
    const std::size_t n = render_control_plane_metrics_prometheus(metrics, buf, sizeof(buf));
    ASSERT_GT(n, 0u);
    const std::string text(buf, n);
    EXPECT_NE(text.find("bytetaper_control_plane_policy_apply_duration_ms "), std::string::npos);
    EXPECT_NE(text.find("bytetaper_control_plane_policy_update_job_duration_ms "),
              std::string::npos);
    EXPECT_NE(text.find("bytetaper_control_plane_policy_apply_stage_duration_ms "),
              std::string::npos);
    EXPECT_EQ(text.find("bytetaper_control_plane_policy_apply_duration_ms_total"),
              std::string::npos);
    EXPECT_EQ(text.find("bytetaper_control_plane_policy_update_job_duration_ms_count"),
              std::string::npos);
}

TEST(ControlPlaneMetricsTest, PrometheusRenderIncludesLowCardinalitySeries) {
    ControlPlaneMetrics metrics{};
    record_policy_cas_conflict(&metrics);
    metrics.policy_version_stored_total.fetch_add(1, std::memory_order_relaxed);
    metrics.policy_update_queue_depth.store(3, std::memory_order_relaxed);
    metrics.manual_adopt_failed_total.fetch_add(1, std::memory_order_relaxed);
    update_fleet_metrics(&metrics, 2, 1, 1, 0, 0, 0, false);
    char buf[8192];
    const std::size_t n = render_control_plane_metrics_prometheus(metrics, buf, sizeof(buf));
    ASSERT_GT(n, 0u);
    const std::string text(buf, n);
    EXPECT_NE(text.find("bytetaper_control_plane_policy_cas_conflict_total"), std::string::npos);
    EXPECT_NE(text.find("bytetaper_control_plane_policy_version_stored_total"), std::string::npos);
    EXPECT_NE(text.find("bytetaper_control_plane_policy_update_queue_depth"), std::string::npos);
    EXPECT_NE(text.find("bytetaper_control_plane_manual_adopt_failed_total"), std::string::npos);
    EXPECT_NE(text.find("bytetaper_control_plane_fleet_runtime_count"), std::string::npos);
    EXPECT_EQ(text.find("jobId"), std::string::npos);
    EXPECT_EQ(text.find("requestId"), std::string::npos);
}

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/policy_activation_result.h"
#include "runtime_policy/runtime_policy_metrics.h"

#include <gtest/gtest.h>

using namespace bytetaper::operational;
using namespace bytetaper::runtime_policy;

TEST(RuntimePolicyMetricsTest, ActivationSuccessIncrementsCounters) {
    RuntimePolicyMetrics metrics{};
    record_activation_success(&metrics, 120, 7);
    EXPECT_EQ(metrics.activation_total.load(), 1u);
    EXPECT_EQ(metrics.activation_duration_ms_total.load(), 120u);
    EXPECT_EQ(metrics.activation_duration_ms_count.load(), 1u);
    EXPECT_EQ(metrics.active_generation.load(), 7u);
    EXPECT_EQ(metrics.activation_status.load(), 1u);
}

TEST(RuntimePolicyMetricsTest, ActivationFailureIncrementsFailedCounter) {
    RuntimePolicyMetrics metrics{};
    record_activation_failure(&metrics, "snapshot_swapped");
    EXPECT_EQ(metrics.activation_failed_total.load(), 1u);
    EXPECT_EQ(metrics.activation_status.load(), 2u);
}

TEST(RuntimePolicyMetricsTest, ActivationStageDurationIncrementsPerStage) {
    RuntimePolicyMetrics metrics{};
    record_activation_stage_duration(
        &metrics, policy_activation_stage_index(PolicyActivationStage::RouteEpochsBumped), 15);
    const std::size_t idx = policy_activation_stage_index(PolicyActivationStage::RouteEpochsBumped);
    EXPECT_EQ(metrics.activation_stage_duration_ms[idx].count.load(), 1u);
    EXPECT_EQ(metrics.activation_stage_duration_ms[idx].total.load(), 15u);
}

TEST(RuntimePolicyMetricsTest, MismatchDetectionIncrementsMismatchAndReasonCounters) {
    RuntimePolicyMetrics metrics{};
    record_mismatch(&metrics, "stale");
    EXPECT_EQ(metrics.mismatch_total.load(), 1u);
    EXPECT_EQ(metrics.stale_local_total.load(), 1u);

    record_mismatch(&metrics, "corrupted");
    EXPECT_EQ(metrics.mismatch_total.load(), 2u);
    EXPECT_EQ(metrics.corrupted_local_total.load(), 1u);
}

TEST(RuntimePolicyMetricsTest, RuntimeConvergenceIncrementsStatusCounters) {
    RuntimePolicyMetrics metrics{};
    record_runtime_convergence(&metrics, "converged");
    record_runtime_convergence(&metrics, "stale");
    record_runtime_convergence(&metrics, "failed");
    EXPECT_EQ(metrics.runtime_converged_total.load(), 1u);
    EXPECT_EQ(metrics.runtime_stale_total.load(), 1u);
    EXPECT_EQ(metrics.runtime_activation_failed_total.load(), 1u);
}

TEST(RuntimePolicyMetricsTest, ActivationOperationalCountsUpdateBarrierMetrics) {
    RuntimePolicyMetrics metrics{};
    record_activation_operational_counts(&metrics, 4, 2, 1, 3, true);
    EXPECT_EQ(metrics.route_epochs_bumped_total.load(), 4u);
    EXPECT_EQ(metrics.l1_cleanup_enqueued_total.load(), 2u);
    EXPECT_EQ(metrics.l2_cleanup_enqueued_total.load(), 1u);
    EXPECT_EQ(metrics.materialized_variant_invalidated_total.load(), 3u);
    EXPECT_EQ(metrics.cleanup_pending.load(), 1u);
}

TEST(RuntimePolicyMetricsTest, PrometheusRenderIncludesBarrierAndConvergenceSeries) {
    RuntimePolicyMetrics metrics{};
    record_activation_success(&metrics, 50, 9);
    record_activation_operational_counts(&metrics, 1, 0, 0, 0, false);
    record_mismatch(&metrics, "diverged");
    record_runtime_convergence(&metrics, "converged");
    char buf[8192];
    const std::size_t n = render_runtime_policy_metrics_prometheus(metrics, buf, sizeof(buf));
    ASSERT_GT(n, 0u);
    const std::string text(buf, n);
    EXPECT_NE(text.find("bytetaper_runtime_policy_active_generation"), std::string::npos);
    EXPECT_NE(text.find("bytetaper_runtime_policy_activation_duration_ms "), std::string::npos);
    EXPECT_EQ(text.find("bytetaper_runtime_policy_activation_duration_ms_total"),
              std::string::npos);
    EXPECT_NE(text.find("bytetaper_runtime_policy_route_epochs_bumped_total"), std::string::npos);
    EXPECT_NE(text.find("bytetaper_runtime_policy_diverged_local_total"), std::string::npos);
    EXPECT_NE(text.find("bytetaper_runtime_policy_runtime_converged_total"), std::string::npos);
}

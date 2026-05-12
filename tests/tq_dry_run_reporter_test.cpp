// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_dry_run_reporter.h"

#include <gtest/gtest.h>

namespace bytetaper::taperquery {

namespace {

TqPolicyChangePlan create_dummy_plan() {
    TqPolicyChangePlan plan;
    plan.status = TqPlanStatus::Ready;
    plan.cas_status = TqCasStatus::Match;
    plan.current_identity = "cur_sha_123";
    plan.candidate_identity = "cand_sha_456";
    plan.expected_base_identity = "cur_sha_123";
    plan.semantic_validation_ok = true;

    plan.risk_summary.highest_risk = TqRiskLevel::High;
    plan.risk_summary.added_routes = 1;
    plan.risk_summary.removed_routes = 1;
    plan.risk_summary.updated_routes = 1;
    plan.risk_summary.reordered_routes = 1;

    // Added route
    TqRouteChange r1;
    r1.kind = TqLegacyRouteChangeKind::Added;
    r1.route_id = "new_route";
    r1.risk = TqRiskLevel::Medium;
    plan.route_changes.push_back(r1);

    // Removed route
    TqRouteChange r2;
    r2.kind = TqLegacyRouteChangeKind::Removed;
    r2.route_id = "old_route";
    r2.risk = TqRiskLevel::High;
    plan.route_changes.push_back(r2);

    // Updated route
    TqRouteChange r3;
    r3.kind = TqLegacyRouteChangeKind::Updated;
    r3.route_id = "existing_route";
    r3.risk = TqRiskLevel::Low;
    r3.order_changed = false;

    PolicyIrFieldDiff fd;
    fd.field_path = "cache.ttl_ms";
    fd.expected = "30000";
    fd.actual = "60000";
    r3.field_diffs.push_back(fd);
    plan.route_changes.push_back(r3);

    // Reordered route
    TqRouteChange r4;
    r4.kind = TqLegacyRouteChangeKind::Reordered;
    r4.route_id = "reordered_route";
    r4.risk = TqRiskLevel::High;
    plan.route_changes.push_back(r4);

    plan.message = "Some warning or success message";
    return plan;
}

} // namespace

TEST(TqDryRunReporterTest, ReportIncludesIdentities) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report = render_taperquery_dry_run_text(plan);

    EXPECT_NE(report.find("Current identity:   cur_sha_123"), std::string::npos);
    EXPECT_NE(report.find("Candidate identity: cand_sha_456"), std::string::npos);
    EXPECT_NE(report.find("Expected base:      cur_sha_123"), std::string::npos);
}

TEST(TqDryRunReporterTest, ReportIncludesCasStatus) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report = render_taperquery_dry_run_text(plan);

    EXPECT_NE(report.find("CAS status:         match"), std::string::npos);
}

TEST(TqDryRunReporterTest, ReportIncludesValidationStatus) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report = render_taperquery_dry_run_text(plan);

    EXPECT_NE(report.find("Validation:         ok"), std::string::npos);
}

TEST(TqDryRunReporterTest, ReportIncludesSummaryCounts) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report = render_taperquery_dry_run_text(plan);

    EXPECT_NE(report.find("Added routes:       1"), std::string::npos);
    EXPECT_NE(report.find("Removed routes:     1"), std::string::npos);
    EXPECT_NE(report.find("Updated routes:     1"), std::string::npos);
    EXPECT_NE(report.find("Reordered routes:   1"), std::string::npos);
}

TEST(TqDryRunReporterTest, ReportIncludesRouteAddUpdateRemoveMarkers) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report = render_taperquery_dry_run_text(plan);

    EXPECT_NE(report.find("+ new_route"), std::string::npos);
    EXPECT_NE(report.find("- old_route"), std::string::npos);
    EXPECT_NE(report.find("~ existing_route"), std::string::npos);
    EXPECT_NE(report.find("! reordered_route"), std::string::npos);
}

TEST(TqDryRunReporterTest, ReportIncludesFieldDiffs) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report = render_taperquery_dry_run_text(plan);

    EXPECT_NE(report.find("cache.ttl_ms: 30000 -> 60000"), std::string::npos);
}

TEST(TqDryRunReporterTest, ReportIncludesRiskLevel) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report = render_taperquery_dry_run_text(plan);

    EXPECT_NE(report.find("Highest risk:       high"), std::string::npos);
    EXPECT_NE(report.find("+ new_route [medium]"), std::string::npos);
    EXPECT_NE(report.find("- old_route [high]"), std::string::npos);
    EXPECT_NE(report.find("~ existing_route [low]"), std::string::npos);
    EXPECT_NE(report.find("! reordered_route [high]"), std::string::npos);
}

TEST(TqDryRunReporterTest, ReportIsDeterministic) {
    TqPolicyChangePlan plan = create_dummy_plan();
    std::string report1 = render_taperquery_dry_run_text(plan);
    std::string report2 = render_taperquery_dry_run_text(plan);

    EXPECT_EQ(report1, report2);
}

TEST(TqDryRunReporterTest, DryRunReportsNoChanges) {
    TqPolicyChangePlan plan;
    plan.status = TqPlanStatus::Ready;
    plan.cas_status = TqCasStatus::NotRequired;
    plan.current_identity = "sha_abc";
    plan.candidate_identity = "sha_abc";
    plan.semantic_validation_ok = true;

    std::string report = render_taperquery_dry_run_text(plan);
    EXPECT_NE(report.find("No route changes"), std::string::npos);
}

} // namespace bytetaper::taperquery

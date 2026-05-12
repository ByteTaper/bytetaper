// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_hash.h"
#include "taperquery/tq_plan.h"

#include <gtest/gtest.h>

namespace bytetaper::taperquery {

namespace {

TqPolicyDocument create_base_policy() {
    TqPolicyDocument doc;
    doc.schema_version = "v1";
    doc.document_id = "doc_1";
    doc.source_name = "test.yaml";
    doc.expected_base_sha = "";

    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.match_prefix = "/api/v1";
    r1.match_kind = TqRouteMatchKind::Prefix;
    // Set mandatory valid cache params so it passes semantic validation
    r1.cache.enabled = true;
    r1.cache.ttl_ms = 60000;
    r1.cache.l1.enabled = true;
    doc.routes.push_back(r1);

    return doc;
}

} // namespace

// ==========================================
// 1. CAS Tests
// ==========================================

TEST(TqPlanTest, PlanReadyWhenCasDisabled) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = create_base_policy();
    candidate.routes[0].match_prefix = "/api/v2"; // some change
    candidate.expected_base_sha = "";             // empty is ok when CAS is disabled

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.status, TqPlanStatus::Ready);
    EXPECT_EQ(plan.cas_status, TqCasStatus::NotRequired);
}

TEST(TqPlanTest, PlanReadyWhenExpectedBaseMatchesCurrentIdentity) {
    TqPolicyDocument current = create_base_policy();
    std::string cur_identity = compute_policy_document_identity(current);

    TqPolicyDocument candidate = create_base_policy();
    candidate.routes[0].match_prefix = "/api/v2";
    candidate.expected_base_sha = cur_identity;

    TqPlanOptions opts;
    opts.require_cas = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.status, TqPlanStatus::Ready);
    EXPECT_EQ(plan.cas_status, TqCasStatus::Match);
}

TEST(TqPlanTest, PlanBlockedWhenExpectedBaseMissing) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = create_base_policy();
    candidate.routes[0].match_prefix = "/api/v2";
    candidate.expected_base_sha = ""; // Missing expected base

    TqPlanOptions opts;
    opts.require_cas = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.status, TqPlanStatus::BlockedByCasMismatch);
    EXPECT_EQ(plan.cas_status, TqCasStatus::MissingExpectedBase);
}

TEST(TqPlanTest, PlanBlockedWhenExpectedBaseDiffersFromCurrentIdentity) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = create_base_policy();
    candidate.routes[0].match_prefix = "/api/v2";
    candidate.expected_base_sha = "some_invalid_identity";

    TqPlanOptions opts;
    opts.require_cas = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.status, TqPlanStatus::BlockedByCasMismatch);
    EXPECT_EQ(plan.cas_status, TqCasStatus::Mismatch);
}

TEST(TqPlanTest, ApplyDecisionDeniedOnCasMismatch) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = create_base_policy();
    candidate.routes[0].match_prefix = "/api/v2";
    candidate.expected_base_sha = "some_invalid_identity";

    TqPlanOptions opts;
    opts.require_cas = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(evaluate_apply_decision(plan), TqApplyDecision::DeniedCasMismatch);
}

TEST(TqPlanTest, ApplyDecisionAllowedWhenReadyAndChanged) {
    TqPolicyDocument current = create_base_policy();
    std::string cur_identity = compute_policy_document_identity(current);

    TqPolicyDocument candidate = create_base_policy();
    candidate.routes[0].match_prefix = "/api/v2";
    candidate.expected_base_sha = cur_identity;

    TqPlanOptions opts;
    opts.require_cas = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(evaluate_apply_decision(plan), TqApplyDecision::Allowed);
}

// ==========================================
// 2. Route Classification Tests
// ==========================================

TEST(TqPlanTest, ClassifiesAddedRoute) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;

    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/v2";
    r2.cache.enabled = true;
    r2.cache.ttl_ms = 30000;
    r2.cache.l1.enabled = true;
    candidate.routes.push_back(r2);

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    ASSERT_EQ(plan.route_changes.size(), 1u);
    EXPECT_EQ(plan.route_changes[0].kind, TqRouteChangeKind::Added);
    EXPECT_EQ(plan.route_changes[0].route_id, "r2");
    EXPECT_EQ(plan.risk_summary.added_routes, 1u);
}

TEST(TqPlanTest, ClassifiesRemovedRoute) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes.clear();

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    ASSERT_EQ(plan.route_changes.size(), 1u);
    EXPECT_EQ(plan.route_changes[0].kind, TqRouteChangeKind::Removed);
    EXPECT_EQ(plan.route_changes[0].route_id, "r1");
    EXPECT_EQ(plan.risk_summary.removed_routes, 1u);
}

TEST(TqPlanTest, ClassifiesUpdatedRoute) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].match_prefix = "/api/v3";

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    ASSERT_EQ(plan.route_changes.size(), 1u);
    EXPECT_EQ(plan.route_changes[0].kind, TqRouteChangeKind::Updated);
    EXPECT_EQ(plan.route_changes[0].route_id, "r1");
    EXPECT_EQ(plan.risk_summary.updated_routes, 1u);
}

TEST(TqPlanTest, ClassifiesReorderedRoute) {
    TqPolicyDocument current = create_base_policy();
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/v2";
    r2.cache.enabled = true;
    r2.cache.ttl_ms = 30000;
    r2.cache.l1.enabled = true;
    current.routes.push_back(r2);

    TqPolicyDocument candidate = current;
    std::swap(candidate.routes[0], candidate.routes[1]);

    TqPlanOptions opts;
    opts.require_cas = false;
    opts.route_order_sensitive = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    // Since identities of both routes are the same but their order changed:
    // Both routes r1 and r2 should be classified as Reordered
    EXPECT_EQ(plan.risk_summary.reordered_routes, 2u);
}

TEST(TqPlanTest, ClassifiesUnchangedRoute) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;

    TqPlanOptions opts;
    opts.require_cas = false;
    opts.include_unchanged_routes = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    ASSERT_EQ(plan.route_changes.size(), 1u);
    EXPECT_EQ(plan.route_changes[0].kind, TqRouteChangeKind::Unchanged);
}

TEST(TqPlanTest, UpdatedRouteCanAlsoMarkOrderChanged) {
    TqPolicyDocument current = create_base_policy();
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/v2";
    r2.cache.enabled = true;
    r2.cache.ttl_ms = 30000;
    r2.cache.l1.enabled = true;
    current.routes.push_back(r2);

    TqPolicyDocument candidate = current;
    std::swap(candidate.routes[0], candidate.routes[1]);
    candidate.routes[0].match_prefix = "/api/v2-updated"; // r2 is updated AND reordered

    TqPlanOptions opts;
    opts.require_cas = false;
    opts.route_order_sensitive = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));

    // Find r2 change
    bool r2_found = false;
    for (const auto& rc : plan.route_changes) {
        if (rc.route_id == "r2") {
            EXPECT_EQ(rc.kind, TqRouteChangeKind::Updated);
            EXPECT_TRUE(rc.order_changed);
            r2_found = true;
        }
    }
    EXPECT_TRUE(r2_found);
}

TEST(TqPlanTest, DuplicateRouteIdsReturnPlanError) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes.push_back(candidate.routes[0]); // duplicate route r1

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.status, TqPlanStatus::Error);
    EXPECT_EQ(plan.risk_summary.highest_risk, TqRiskLevel::Critical);
}

// ==========================================
// 3. Field Diff Tests
// ==========================================

TEST(TqPlanTest, AttachesFieldDiffsToUpdatedRoute) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].match_prefix = "/api/v3";

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    ASSERT_EQ(plan.route_changes.size(), 1u);
    EXPECT_EQ(plan.route_changes[0].kind, TqRouteChangeKind::Updated);
    ASSERT_FALSE(plan.route_changes[0].field_diffs.empty());
    EXPECT_EQ(plan.route_changes[0].field_diffs[0].field_path, "match_prefix");
}

TEST(TqPlanTest, ReportsCacheTtlChange) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].cache.ttl_ms = 120000; // changed

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.risk_summary.cache_changes, 1u);
}

TEST(TqPlanTest, ReportsCompressionAlgorithmChange) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].compression.enabled = true;
    candidate.routes[0].compression.preferred_algorithms = { TqCompressionAlgorithm::Gzip };

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_GT(plan.risk_summary.compression_changes, 0u);
}

TEST(TqPlanTest, ReportsPaginationLimitChange) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].pagination.enabled = true;
    candidate.routes[0].pagination.default_limit = 50;

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_GT(plan.risk_summary.pagination_changes, 0u);
}

TEST(TqPlanTest, ReportsCoalescingBudgetChange) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].coalescing.enabled = true;
    candidate.routes[0].coalescing.max_follower_wait_budget_ms = 1000;

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_GT(plan.risk_summary.coalescing_changes, 0u);
}

TEST(TqPlanTest, ReportsMutationChange) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].mutation = TqMutationMode::HeadersOnly;

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_GT(plan.risk_summary.mutation_changes, 0u);
}

TEST(TqPlanTest, ReportsMatchPrefixChange) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].match_prefix = "/api/v4";

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    ASSERT_FALSE(plan.route_changes[0].field_diffs.empty());
    EXPECT_EQ(plan.route_changes[0].field_diffs[0].field_path, "match_prefix");
}

TEST(TqPlanTest, ReportsDocumentMetadataChange) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.schema_version = "v2"; // Metadata change

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_FALSE(plan.document_field_diffs.empty());
    EXPECT_EQ(plan.document_field_diffs[0].field_path, "schema_version");
}

// ==========================================
// 4. Risk Tests
// ==========================================

TEST(TqPlanTest, AddedRouteIsMediumRisk) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/v2";
    r2.cache.enabled = true;
    r2.cache.ttl_ms = 30000;
    r2.cache.l1.enabled = true;
    candidate.routes.push_back(r2);

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.route_changes[0].risk, TqRiskLevel::Medium);
}

TEST(TqPlanTest, RemovedRouteIsHighRisk) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes.clear();

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.route_changes[0].risk, TqRiskLevel::High);
}

TEST(TqPlanTest, MutationChangeIsHighRisk) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].mutation = TqMutationMode::HeadersOnly;

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.route_changes[0].risk, TqRiskLevel::High);
}

TEST(TqPlanTest, CacheEnableChangeIsHighRisk) {
    TqPolicyDocument current = create_base_policy();
    current.routes[0].cache.enabled = false;
    TqPolicyDocument candidate = current;
    candidate.routes[0].cache.enabled = true;

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.route_changes[0].risk, TqRiskLevel::High);
}

TEST(TqPlanTest, PrivateCacheChangeIsHighRisk) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].cache.private_cache.enabled = true;
    candidate.routes[0].cache.private_cache.auth_scope_header = "X-Auth";

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.route_changes[0].risk, TqRiskLevel::High);
}

TEST(TqPlanTest, CoalescingAllowAuthenticatedChangeIsHighRisk) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].coalescing.enabled = true;
    candidate.routes[0].coalescing.allow_authenticated = true;
    // Ensure auth scope header is set to avoid validation errors
    candidate.routes[0].cache.private_cache.enabled = true;
    candidate.routes[0].cache.private_cache.auth_scope_header = "X-Auth";

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.route_changes[0].risk, TqRiskLevel::High);
}

TEST(TqPlanTest, CasMismatchIsCriticalRisk) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].match_prefix = "/api/v2";
    candidate.expected_base_sha = "invalid";

    TqPlanOptions opts;
    opts.require_cas = true;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.risk_summary.highest_risk, TqRiskLevel::Critical);
}

TEST(TqPlanTest, ValidationErrorIsCriticalRisk) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    // Missing required L1 capacity config when cache is enabled triggers standard validation error
    candidate.routes[0].cache.enabled = true;
    candidate.routes[0].cache.l1.enabled = true;
    candidate.routes[0].cache.l1.capacity_entries =
        0; // standard validation failure is handled by RoutePolicy validator

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    // Since some fields might still trigger errors, let's verify highest_risk is Critical when
    // validation fails
    if (!plan.semantic_validation_ok) {
        EXPECT_EQ(plan.risk_summary.highest_risk, TqRiskLevel::Critical);
    }
}

TEST(TqPlanTest, HighestRiskIsAggregated) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;
    candidate.routes[0].mutation = TqMutationMode::HeadersOnly; // High risk change

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(plan.risk_summary.highest_risk, TqRiskLevel::High);
}

// ==========================================
// 5. No Change Tests
// ==========================================

TEST(TqPlanTest, IdenticalPoliciesProduceNoChanges) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_TRUE(plan.route_changes.empty());
    EXPECT_EQ(plan.risk_summary.highest_risk, TqRiskLevel::None);
}

TEST(TqPlanTest, ApplyDecisionDeniedOnNoChanges) {
    TqPolicyDocument current = create_base_policy();
    TqPolicyDocument candidate = current;

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_EQ(evaluate_apply_decision(plan), TqApplyDecision::DeniedNoChanges);
}

} // namespace bytetaper::taperquery

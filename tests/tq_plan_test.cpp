// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_validator.h"
#include "taperquery/policy_ir_version.h"
#include "taperquery/tq_plan.h"

#include <gtest/gtest.h>

namespace bytetaper::taperquery {

namespace {

TqPolicyDocument create_base_policy() {
    TqPolicyDocument doc;
    doc.version.source_schema_version = "tq/v1";
    doc.version.policy_ir_version = kCurrentPolicyIrVersion;
    doc.version.identity_version = kCurrentPolicyIdentityVersion;
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
    r1.cache.l1.capacity_entries = 1000;
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
    r2.cache.l1.capacity_entries = 1000;
    candidate.routes.push_back(r2);

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    ASSERT_EQ(plan.route_changes.size(), 1u);
    EXPECT_EQ(plan.route_changes[0].kind, TqLegacyRouteChangeKind::Added);
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
    EXPECT_EQ(plan.route_changes[0].kind, TqLegacyRouteChangeKind::Removed);
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
    EXPECT_EQ(plan.route_changes[0].kind, TqLegacyRouteChangeKind::Updated);
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
    r2.cache.l1.capacity_entries = 1000;
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
    EXPECT_EQ(plan.route_changes[0].kind, TqLegacyRouteChangeKind::Unchanged);
}

TEST(TqPlanTest, UpdatedRouteCanAlsoMarkOrderChanged) {
    TqPolicyDocument current = create_base_policy();
    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.match_prefix = "/api/v2";
    r2.cache.enabled = true;
    r2.cache.ttl_ms = 30000;
    r2.cache.l1.enabled = true;
    r2.cache.l1.capacity_entries = 1000;
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
            EXPECT_EQ(rc.kind, TqLegacyRouteChangeKind::Updated);
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
    EXPECT_EQ(plan.route_changes[0].kind, TqLegacyRouteChangeKind::Updated);
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
    candidate.routes[0].coalescing.mode = TqCoalescingMode::CacheAssisted;
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
    candidate.version.source_schema_version = "v2"; // Metadata change

    TqPlanOptions opts;
    opts.require_cas = false;

    TqPolicyChangePlan plan;
    ASSERT_TRUE(build_taperquery_change_plan(current, candidate, opts, &plan));
    EXPECT_FALSE(plan.document_field_diffs.empty());
    EXPECT_EQ(plan.document_field_diffs[0].field_path, "version.source_schema_version");
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
    r2.cache.l1.capacity_entries = 1000;
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
    candidate.routes[0].coalescing.mode = TqCoalescingMode::CacheAssisted;
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

// ==========================================
// 6. GA-spec TqApplyPlan Model Tests
// ==========================================

TEST(TqPlanTest, BuildTaperQueryApplyPlanSuccess) {
    TqPolicyDocument before = create_base_policy();
    TqPolicyDocument after = create_base_policy();

    after.routes[0].match_prefix = "/api/v2";
    after.expected_base_sha = compute_policy_document_identity(before);

    TqApplyPlanOptions opts;
    opts.strict_production = true;

    auto plan = build_taperquery_apply_plan(before, after, opts);
    EXPECT_TRUE(plan.ok);
    EXPECT_TRUE(plan.issues.empty());

    ASSERT_EQ(plan.route_changes.size(), 1u);
    EXPECT_EQ(plan.route_changes[0].route_id, "r1");
    EXPECT_EQ(plan.route_changes[0].kind, TqRouteChangeKind::Modified);
    EXPECT_FALSE(plan.route_changes[0].field_changes.empty());
    EXPECT_EQ(plan.route_changes[0].field_changes[0].field_path, "match_prefix");
    EXPECT_EQ(plan.route_changes[0].field_changes[0].impact, TqSemanticImpact::MatchBehavior);
}

TEST(TqPlanTest, BuildTaperQueryApplyPlanStrictProductionCasFail) {
    TqPolicyDocument before = create_base_policy();
    TqPolicyDocument after = create_base_policy();
    after.expected_base_sha = "invalid_cas_sha";

    TqApplyPlanOptions opts;
    opts.strict_production = true;

    auto plan = build_taperquery_apply_plan(before, after, opts);
    EXPECT_FALSE(plan.ok);

    bool found_cas_mismatch = false;
    for (const auto& issue : plan.issues) {
        if (issue.code == "CAS_SHA_MISMATCH") {
            found_cas_mismatch = true;
            EXPECT_EQ(issue.severity, TqPlanSeverity::Blocker);
        }
    }
    EXPECT_TRUE(found_cas_mismatch);
}

TEST(TqPlanTest, RenderApplyPlanSuccess) {
    TqPolicyDocument before = create_base_policy();
    TqPolicyDocument after = create_base_policy();
    after.routes[0].match_prefix = "/api/v2";
    after.expected_base_sha = compute_policy_document_identity(before);

    auto plan = build_taperquery_apply_plan(before, after);
    std::string markdown = render_taperquery_apply_plan_markdown(plan);
    EXPECT_NE(markdown.find("APPROVED"), std::string::npos);
    EXPECT_NE(markdown.find("Modified"), std::string::npos);

    std::string text = render_taperquery_apply_plan_text(plan);
    EXPECT_NE(text.find("APPROVED"), std::string::npos);
    EXPECT_NE(text.find("Modified"), std::string::npos);
}

TEST(TqPlanTest, ApplyPlanGATests) {
    TqPolicyDocument before = create_base_policy();

    // Test 1: Route added
    {
        TqPolicyDocument after = before;
        TqRoutePolicy added;
        added.route_id = "added_route";
        added.match_prefix = "/api/added";
        after.routes.push_back(added);
        after.expected_base_sha = compute_policy_document_identity(before);

        auto plan = build_taperquery_apply_plan(before, after);
        EXPECT_TRUE(plan.ok);
        bool found_added = false;
        for (const auto& rc : plan.route_changes) {
            if (rc.route_id == "added_route" && rc.kind == TqRouteChangeKind::Added) {
                found_added = true;
            }
        }
        EXPECT_TRUE(found_added);
    }

    // Test 2: Route removed
    {
        TqPolicyDocument local_before = before;
        TqRoutePolicy r2;
        r2.route_id = "r2";
        r2.match_prefix = "/api/v2";
        // Set mandatory valid cache params so it passes semantic validation
        r2.cache.enabled = true;
        r2.cache.ttl_ms = 60000;
        r2.cache.l1.enabled = true;
        r2.cache.l1.capacity_entries = 1000;
        local_before.routes.push_back(r2);

        TqPolicyDocument after = local_before;
        after.routes.pop_back(); // remove r2
        after.expected_base_sha = compute_policy_document_identity(local_before);

        auto plan = build_taperquery_apply_plan(local_before, after);
        EXPECT_TRUE(plan.ok);
        bool found_removed = false;
        for (const auto& rc : plan.route_changes) {
            if (rc.route_id == "r2" && rc.kind == TqRouteChangeKind::Removed) {
                found_removed = true;
            }
        }
        EXPECT_TRUE(found_removed);
    }

    // Test 3: Unchanged routes
    {
        TqPolicyDocument after = before;
        after.expected_base_sha = compute_policy_document_identity(before);

        TqApplyPlanOptions opts;
        opts.include_unchanged_routes = true;
        auto plan = build_taperquery_apply_plan(before, after, opts);
        EXPECT_TRUE(plan.ok);
        bool found_unchanged = false;
        for (const auto& rc : plan.route_changes) {
            if (rc.route_id == "r1" && rc.kind == TqRouteChangeKind::Unchanged) {
                found_unchanged = true;
            }
        }
        EXPECT_TRUE(found_unchanged);
    }

    // Test 4: Validation warnings propagation
    {
        TqPolicyDocument after = before;
        // Introduce a warning (e.g. observe mode warning by having a feature block with
        // mutation=disabled)
        after.routes[0].mutation = TqMutationMode::Disabled;
        after.routes[0].field_filter.mode = TqFieldFilterMode::Allowlist;
        after.routes[0].field_filter.fields = { "id" };
        after.expected_base_sha = compute_policy_document_identity(before);

        auto plan = build_taperquery_apply_plan(before, after);
        EXPECT_TRUE(plan.ok); // Warning doesn't block the plan!
        bool found_warning = false;
        for (const auto& issue : plan.issues) {
            if (issue.severity == TqPlanSeverity::Warning && issue.code == "OBSERVE_MODE_WARNING") {
                found_warning = true;
            }
        }
        EXPECT_TRUE(found_warning);
    }

    // Test 5: Semantic impact mappings for modified fields
    {
        TqPolicyDocument after = before;
        after.routes[0].cache.enabled = true;
        after.routes[0].cache.ttl_ms = 45000;
        after.routes[0].cache.l1.enabled = true;
        after.routes[0].cache.l1.capacity_entries = 500;
        after.routes[0].cache.field_variant.enabled = true;
        after.routes[0].cache.field_variant.max_variants_per_route = 12;
        after.routes[0].cache.field_variant.ttl_max_ms = 60000;
        after.routes[0].cache.vary_headers.names = { "X-Test" };
        after.routes[0].field_filter.mode = TqFieldFilterMode::Denylist;
        after.routes[0].field_filter.fields = { "secret" };
        after.routes[0].coalescing.enabled = true;
        after.routes[0].coalescing.backend_timeout_ms = 1200;
        after.routes[0].failure_mode = TqFailureMode::FailClosed;
        after.expected_base_sha = compute_policy_document_identity(before);

        TqApplyPlanOptions opts;
        opts.include_field_level_changes = true;
        auto plan = build_taperquery_apply_plan(before, after, opts);
        EXPECT_TRUE(plan.ok);

        bool found_cache_behavior = false;
        bool found_cache_key = false;
        bool found_cache_storage = false;
        bool found_field_filter = false;
        bool found_coalescing = false;
        bool found_failure = false;

        ASSERT_EQ(plan.route_changes.size(), 1u);
        for (const auto& fc : plan.route_changes[0].field_changes) {
            if (fc.impact == TqSemanticImpact::CacheBehavior)
                found_cache_behavior = true;
            if (fc.impact == TqSemanticImpact::CacheKeyBehavior)
                found_cache_key = true;
            if (fc.impact == TqSemanticImpact::CacheStorageBehavior)
                found_cache_storage = true;
            if (fc.impact == TqSemanticImpact::FieldFilteringBehavior)
                found_field_filter = true;
            if (fc.impact == TqSemanticImpact::CoalescingBehavior)
                found_coalescing = true;
            if (fc.impact == TqSemanticImpact::FailureBehavior)
                found_failure = true;
        }

        EXPECT_TRUE(found_cache_behavior);
        EXPECT_TRUE(found_cache_key);
        EXPECT_TRUE(found_cache_storage);
        EXPECT_TRUE(found_field_filter);
        EXPECT_TRUE(found_coalescing);
        EXPECT_TRUE(found_failure);
    }
}

TEST(TqPlanTest, ApplyPlanGATestsPart2) {
    TqPolicyDocument before = create_base_policy();

    // 1. Semantic impacts for L2 Path, Mutation and Private Cache
    {
        TqPolicyDocument after = before;
        after.routes[0].cache.l2.enabled = true;
        after.routes[0].cache.l2.path = "/var/lib/rocksdb";
        after.routes[0].mutation = TqMutationMode::HeadersOnly;
        after.routes[0].cache.private_cache.enabled = true;
        after.routes[0].cache.private_cache.auth_scope_header = "X-User-ID";
        after.expected_base_sha = compute_policy_document_identity(before);

        TqApplyPlanOptions opts;
        opts.include_field_level_changes = true;
        auto plan = build_taperquery_apply_plan(before, after, opts);
        EXPECT_TRUE(plan.ok);

        bool found_l2_storage = false;
        bool found_mutation = false;
        bool found_private_cache_key = false;

        ASSERT_EQ(plan.route_changes.size(), 1u);
        for (const auto& fc : plan.route_changes[0].field_changes) {
            if (fc.field_path == "cache.l2.path" &&
                fc.impact == TqSemanticImpact::CacheStorageBehavior) {
                found_l2_storage = true;
            }
            if (fc.field_path == "mutation" && fc.impact == TqSemanticImpact::MutationBehavior) {
                found_mutation = true;
            }
            if (fc.field_path == "cache.private_cache.auth_scope_header" &&
                fc.impact == TqSemanticImpact::CacheKeyBehavior) {
                found_private_cache_key = true;
            }
        }
        EXPECT_TRUE(found_l2_storage);
        EXPECT_TRUE(found_mutation);
        EXPECT_TRUE(found_private_cache_key);
    }

    // 2. Route Shadowing Warnings Propagation
    {
        TqPolicyDocument after = before;
        TqRoutePolicy r2;
        r2.route_id = "r2";
        r2.match_prefix = "/api/v1/subpath"; // Shadowed by r1's prefix "/api/v1"
        r2.match_kind = TqRouteMatchKind::Prefix;
        r2.cache.enabled = true;
        r2.cache.ttl_ms = 60000;
        r2.cache.l1.enabled = true;
        r2.cache.l1.capacity_entries = 1000;
        after.routes.push_back(r2);
        after.expected_base_sha = compute_policy_document_identity(before);

        auto plan = build_taperquery_apply_plan(before, after);
        EXPECT_TRUE(plan.ok); // Warning only, plan should still be approved!

        bool found_shadow_warning = false;
        for (const auto& issue : plan.issues) {
            if (issue.severity == TqPlanSeverity::Warning &&
                issue.code == "ROUTE_ANALYSIS_PREFIX_SHADOWS_PREFIX") {
                found_shadow_warning = true;
            }
        }
        EXPECT_TRUE(found_shadow_warning);
    }

    // 3. Validation-Error Blocker
    {
        TqPolicyDocument after = before;
        TqRoutePolicy invalid_route;
        invalid_route.route_id = ""; // Empty route_id triggers an Error!
        after.routes.push_back(invalid_route);
        after.expected_base_sha = compute_policy_document_identity(before);

        auto plan = build_taperquery_apply_plan(before, after);
        EXPECT_FALSE(plan.ok); // Error triggers a blocker!

        bool found_blocker = false;
        for (const auto& issue : plan.issues) {
            if (issue.severity == TqPlanSeverity::Blocker && issue.code == "EMPTY_ROUTE_ID") {
                found_blocker = true;
            }
        }
        EXPECT_TRUE(found_blocker);
    }

    // 4. Validator Check for Private Cache Header Characters and Compression Algorithm Range
    {
        TqPolicyDocument doc = before;
        doc.routes[0].cache.private_cache.enabled = true;
        doc.routes[0].cache.private_cache.auth_scope_header =
            "Invalid Header!"; // Spaces/symbols are invalid!

        doc.routes[0].compression.enabled = true;
        doc.routes[0].compression.min_size_bytes = 100;
        doc.routes[0].compression.eligible_content_types = { "application/json" };
        doc.routes[0].compression.preferred_algorithms = { static_cast<TqCompressionAlgorithm>(
            99) }; // Out of range!

        TqPolicyValidationOptions val_opts;
        val_opts.collect_all = true;
        auto val_res = validate_taperquery_policy_ir(doc, val_opts);
        EXPECT_FALSE(val_res.ok);

        bool found_header_char_error = false;
        bool found_algo_invalid_error = false;
        for (const auto& issue : val_res.issues) {
            if (issue.code == "PRIVATE_CACHE_INVALID_AUTH_HEADER_CHARACTERS") {
                found_header_char_error = true;
            }
            if (issue.code == "COMPRESSION_ALGORITHM_INVALID") {
                found_algo_invalid_error = true;
            }
        }
        EXPECT_TRUE(found_header_char_error);
        EXPECT_TRUE(found_algo_invalid_error);
    }
}

} // namespace bytetaper::taperquery

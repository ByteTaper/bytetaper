// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/route_cache_epoch_store.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/tq_cache_namespace_versioning.h"

#include "gtest/gtest.h"

namespace bytetaper::taperquery {

TEST(TqCacheNamespaceVersioningTest, FieldFilteringChangeRequiresEpochBump) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "test_route";
    rc.kind = TqRouteChangeKind::Modified;

    TqApplyPlanFieldChange fc;
    fc.field_path = "allowed_fields";
    fc.impact = TqSemanticImpact::FieldFilteringBehavior;
    rc.field_changes.push_back(fc);

    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};
    runtime::route_cache_epoch_register(&store, "test_route");

    auto res = version_cache_namespace_for_apply_plan(plan, &store);
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.changed_routes.size(), 1);
    EXPECT_EQ(res.changed_routes[0].route_id, "test_route");
    EXPECT_TRUE(res.changed_routes[0].epoch_bump_required);
    EXPECT_EQ(res.changed_routes[0].before_epoch, 1);
    EXPECT_EQ(res.changed_routes[0].after_epoch, 2);
}

TEST(TqCacheNamespaceVersioningTest, CacheBehaviorChangeRequiresEpochBump) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "test_route";
    rc.kind = TqRouteChangeKind::Modified;

    TqApplyPlanFieldChange fc;
    fc.field_path = "cache.behavior";
    fc.impact = TqSemanticImpact::CacheBehavior;
    rc.field_changes.push_back(fc);

    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};
    runtime::route_cache_epoch_register(&store, "test_route");

    auto res = version_cache_namespace_for_apply_plan(plan, &store);
    ASSERT_TRUE(res.ok);
    EXPECT_TRUE(res.changed_routes[0].epoch_bump_required);
}

TEST(TqCacheNamespaceVersioningTest, CacheKeyBehaviorChangeRequiresEpochBump) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "test_route";
    rc.kind = TqRouteChangeKind::Modified;

    TqApplyPlanFieldChange fc;
    fc.field_path = "cache.vary_headers.names";
    fc.impact = TqSemanticImpact::CacheKeyBehavior;
    rc.field_changes.push_back(fc);

    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};
    runtime::route_cache_epoch_register(&store, "test_route");

    auto res = version_cache_namespace_for_apply_plan(plan, &store);
    ASSERT_TRUE(res.ok);
    EXPECT_TRUE(res.changed_routes[0].epoch_bump_required);
}

TEST(TqCacheNamespaceVersioningTest, CacheStorageBehaviorChangeRequiresEpochBump) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "test_route";
    rc.kind = TqRouteChangeKind::Modified;

    TqApplyPlanFieldChange fc;
    fc.field_path = "cache.l1.capacity_entries";
    fc.impact = TqSemanticImpact::CacheStorageBehavior;
    rc.field_changes.push_back(fc);

    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};
    runtime::route_cache_epoch_register(&store, "test_route");

    auto res = version_cache_namespace_for_apply_plan(plan, &store);
    ASSERT_TRUE(res.ok);
    EXPECT_TRUE(res.changed_routes[0].epoch_bump_required);
}

TEST(TqCacheNamespaceVersioningTest, UnaffectedRouteEpochUnchanged) {
    TqApplyPlan plan;

    TqApplyPlanRouteChange rc1;
    rc1.route_id = "route_a";
    rc1.kind = TqRouteChangeKind::Modified;
    TqApplyPlanFieldChange fc;
    fc.impact = TqSemanticImpact::FieldFilteringBehavior;
    rc1.field_changes.push_back(fc);
    plan.route_changes.push_back(rc1);

    TqApplyPlanRouteChange rc2;
    rc2.route_id = "route_b";
    rc2.kind = TqRouteChangeKind::Modified; // modified but NO cache impact
    TqApplyPlanFieldChange fc2;
    fc2.impact = TqSemanticImpact::MatchBehavior;
    rc2.field_changes.push_back(fc2);
    plan.route_changes.push_back(rc2);

    runtime::RouteCacheEpochStore store{};
    runtime::route_cache_epoch_register(&store, "route_a");
    runtime::route_cache_epoch_register(&store, "route_b");

    auto res = version_cache_namespace_for_apply_plan(plan, &store);
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.changed_routes.size(), 2);

    for (const auto& ch : res.changed_routes) {
        if (ch.route_id == "route_a") {
            EXPECT_TRUE(ch.epoch_bump_required);
            EXPECT_EQ(ch.after_epoch, 2);
        } else if (ch.route_id == "route_b") {
            EXPECT_FALSE(ch.epoch_bump_required);
            EXPECT_EQ(ch.after_epoch, 1);
        }
    }
}

TEST(TqCacheNamespaceVersioningTest, BumpIncreasesEpoch) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "test_route";
    rc.kind = TqRouteChangeKind::Modified;

    TqApplyPlanFieldChange fc;
    fc.impact = TqSemanticImpact::FieldFilteringBehavior;
    rc.field_changes.push_back(fc);
    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};
    runtime::route_cache_epoch_register(&store, "test_route");

    runtime::route_cache_epoch_reset_for_tests(&store, "test_route", 7);

    auto res = version_cache_namespace_for_apply_plan(plan, &store);
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.changed_routes[0].before_epoch, 7);
    EXPECT_EQ(res.changed_routes[0].after_epoch, 8);
}

TEST(TqCacheNamespaceVersioningTest, DetectDoesNotBumpEpoch) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "test_route";
    rc.kind = TqRouteChangeKind::Modified;

    TqApplyPlanFieldChange fc;
    fc.impact = TqSemanticImpact::FieldFilteringBehavior;
    rc.field_changes.push_back(fc);
    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};
    runtime::route_cache_epoch_register(&store, "test_route");
    runtime::route_cache_epoch_reset_for_tests(&store, "test_route", 7);

    auto res = detect_cache_namespace_impacts(plan, &store);
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.changed_routes[0].before_epoch, 7);
    EXPECT_EQ(res.changed_routes[0].after_epoch, 8);
    EXPECT_TRUE(res.changed_routes[0].epoch_bump_required);

    std::uint64_t current = 0;
    runtime::route_cache_epoch_get(&store, "test_route", &current);
    EXPECT_EQ(current, 7);
}

TEST(TqCacheNamespaceVersioningTest, NewRouteIsRegisteredNotBumped) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "new_route";
    rc.kind = TqRouteChangeKind::Added;
    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};

    auto res = version_cache_namespace_for_apply_plan(plan, &store);
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.changed_routes[0].before_epoch, 1);
    EXPECT_EQ(res.changed_routes[0].after_epoch, 1);
    EXPECT_FALSE(res.changed_routes[0].epoch_bump_required);

    std::uint64_t current = 0;
    auto get_res = runtime::route_cache_epoch_get(&store, "new_route", &current);
    EXPECT_EQ(get_res, runtime::RouteCacheEpochResult::Ok);
    EXPECT_EQ(current, 1);
}

TEST(TqCacheNamespaceVersioningTest, NewRouteDetectedWithoutRegistration) {
    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "new_route";
    rc.kind = TqRouteChangeKind::Added;
    plan.route_changes.push_back(rc);

    runtime::RouteCacheEpochStore store{};

    auto res = detect_cache_namespace_impacts(plan, &store);
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.changed_routes[0].before_epoch, 0);
    EXPECT_EQ(res.changed_routes[0].after_epoch, 1);
    EXPECT_FALSE(res.changed_routes[0].epoch_bump_required);

    // Verify it is NOT registered in store yet
    std::uint64_t current = 0;
    auto get_res = runtime::route_cache_epoch_get(&store, "new_route", &current);
    EXPECT_EQ(get_res, runtime::RouteCacheEpochResult::NotFound);
}

TEST(TqCacheNamespaceVersioningTest, RouteIdentityChangesWhenAllowedFieldsChange) {
    TqRoutePolicy r1;
    r1.route_id = "test";
    r1.field_filter.fields.push_back("id");
    r1.field_filter.fields.push_back("email");

    TqRoutePolicy r2;
    r2.route_id = "test";
    r2.field_filter.fields.push_back("id");

    EXPECT_NE(compute_route_policy_identity(r1), compute_route_policy_identity(r2));
}

} // namespace bytetaper::taperquery

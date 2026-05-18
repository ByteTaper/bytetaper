// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/l1_cache.h"
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

TEST(TqRouteIdentityTest, RouteIdentityChangesWhenAllowedFieldsChange) {
    TqRoutePolicy r1;
    r1.route_id = "test";
    r1.field_filter.fields.push_back("id");
    r1.field_filter.fields.push_back("email");

    TqRoutePolicy r2;
    r2.route_id = "test";
    r2.field_filter.fields.push_back("id");

    EXPECT_NE(compute_route_policy_identity(r1), compute_route_policy_identity(r2));
}

TEST(TqCacheNamespaceCleanupTest, L1CleanupSynchronousSweep) {
    auto cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(cache.get());

    // 1. Populate L1 Cache with matching and non-matching entries
    cache::CacheEntry e1{};
    std::strncpy(e1.key, "GET|route1|epoch:1|/path|||policy_id_v1", sizeof(e1.key) - 1);
    e1.body = "body1";
    e1.body_len = 5;
    cache::l1_put(cache.get(), e1);

    cache::CacheEntry e2{};
    std::strncpy(e2.key, "var:GET|route1|epoch:1|/path|||policy_id_v1", sizeof(e2.key) - 1);
    e2.body = "body2";
    e2.body_len = 5;
    cache::l1_put(cache.get(), e2);

    cache::CacheEntry e3{};
    std::strncpy(e3.key, "GET|route1|epoch:2|/path|||policy_id_v1", sizeof(e3.key) - 1);
    e3.body = "body3";
    e3.body_len = 5;
    cache::l1_put(cache.get(), e3);

    cache::CacheEntry e4{};
    std::strncpy(e4.key, "GET|route2|epoch:1|/path|||policy_id_v1", sizeof(e4.key) - 1);
    e4.body = "body4";
    e4.body_len = 5;
    cache::l1_put(cache.get(), e4);

    // 2. Perform L1 cleanup request
    cache::L1RouteNamespaceCleanupRequest req;
    req.route_id = "route1";
    req.old_epoch = 1;
    req.old_policy_identity = "policy_id_v1";
    req.include_variant_entries = true;

    auto result = cache::l1_cleanup_route_namespace(cache.get(), req);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.removed_count, 2);

    // 3. Verify matching keys are removed, and non-matching keys remain!
    cache::CacheEntry out{};
    char body_buf[128];
    EXPECT_FALSE(cache::l1_get(cache.get(), "GET|route1|epoch:1|/path|||policy_id_v1", 0, &out,
                               body_buf, sizeof(body_buf)));
    EXPECT_FALSE(cache::l1_get(cache.get(), "var:GET|route1|epoch:1|/path|||policy_id_v1", 0, &out,
                               body_buf, sizeof(body_buf)));
    EXPECT_TRUE(cache::l1_get(cache.get(), "GET|route1|epoch:2|/path|||policy_id_v1", 0, &out,
                              body_buf, sizeof(body_buf)));
    EXPECT_TRUE(cache::l1_get(cache.get(), "GET|route2|epoch:1|/path|||policy_id_v1", 0, &out,
                              body_buf, sizeof(body_buf)));
}

TEST(TqCacheNamespaceCleanupTest, L2AsyncCleanupQueueWorkerLoop) {
    RouteCacheCleanupQueueImpl queue;
    queue.start_worker();

    RouteCacheCleanupJob job1{ "route1", 1, 2, "id1", "id2" };
    RouteCacheCleanupJob job2{ "route2", 5, 6, "idA", "idB" };

    queue.enqueue(job1);
    queue.enqueue(job2);

    // Give the worker thread a brief moment to process the queue
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.shutdown();

    auto completed = queue.get_completed_jobs();
    ASSERT_EQ(completed.size(), 2);
    EXPECT_EQ(completed[0].route_id, "route1");
    EXPECT_EQ(completed[1].route_id, "route2");
}

TEST(TqCacheNamespaceCleanupTest, UnifiedApplyOrchestration) {
    runtime::RouteCacheEpochStore epoch_store;
    runtime::route_cache_epoch_register(&epoch_store, "test_route");
    runtime::route_cache_epoch_reset_for_tests(&epoch_store, "test_route", 3);

    auto l1_cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1_cache.get());

    cache::CacheEntry e{};
    std::strncpy(e.key, "GET|test_route|epoch:3|/path|||policy_v1", sizeof(e.key) - 1);
    l1_put(l1_cache.get(), e);

    RouteCacheCleanupQueueImpl l2_queue;
    l2_queue.start_worker();

    TqApplyPlan plan;
    TqApplyPlanRouteChange rc;
    rc.route_id = "test_route";
    rc.kind = TqRouteChangeKind::Modified;
    rc.before_identity = "policy_v1";
    rc.after_identity = "policy_v2";

    TqApplyPlanFieldChange fc;
    fc.impact = TqSemanticImpact::FieldFilteringBehavior;
    rc.field_changes.push_back(fc);
    plan.route_changes.push_back(rc);

    auto result = version_and_cleanup_cache_namespaces_for_apply(plan, &epoch_store, l1_cache.get(),
                                                                 &l2_queue);
    EXPECT_TRUE(result.ok);
    ASSERT_EQ(result.routes.size(), 1);
    EXPECT_EQ(result.routes[0].route_id, "test_route");
    EXPECT_EQ(result.routes[0].old_epoch, 3);
    EXPECT_EQ(result.routes[0].new_epoch, 4);
    EXPECT_TRUE(result.routes[0].l1_cleanup_required);
    EXPECT_EQ(result.routes[0].l1_removed_count, 1);
    EXPECT_TRUE(result.routes[0].l2_cleanup_enqueued);

    l2_queue.shutdown();
}

TEST(TqCacheNamespaceCleanupTest, L1CleanupMatchesRealCacheKeysWithSuffixes) {
    auto cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(cache.get());

    // 1. Keys with vary suffix and scope suffix
    cache::CacheEntry e1{};
    std::strncpy(e1.key,
                 "GET|route1|epoch:1|/path|||policy_id_v1|vary:Accept-Encoding=sha|scope:user",
                 sizeof(e1.key) - 1);
    e1.body = "body1";
    e1.body_len = 5;
    cache::l1_put(cache.get(), e1);

    cache::CacheEntry e2{};
    std::strncpy(e2.key,
                 "GET|route1|epoch:1|/path|||policy_id_v2|vary:Accept-Encoding=sha|scope:user",
                 sizeof(e2.key) - 1);
    e2.body = "body2";
    e2.body_len = 5;
    cache::l1_put(cache.get(), e2);

    // 2. Perform L1 cleanup request for policy_id_v1
    cache::L1RouteNamespaceCleanupRequest req;
    req.route_id = "route1";
    req.old_epoch = 1;
    req.old_policy_identity = "policy_id_v1";
    req.include_variant_entries = true;

    auto result = cache::l1_cleanup_route_namespace(cache.get(), req);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.removed_count, 1);

    // 3. Verify e1 is removed, and e2 remains!
    cache::CacheEntry out{};
    char body_buf[128];
    EXPECT_FALSE(cache::l1_get(
        cache.get(), "GET|route1|epoch:1|/path|||policy_id_v1|vary:Accept-Encoding=sha|scope:user",
        0, &out, body_buf, sizeof(body_buf)));
    EXPECT_TRUE(cache::l1_get(
        cache.get(), "GET|route1|epoch:1|/path|||policy_id_v2|vary:Accept-Encoding=sha|scope:user",
        0, &out, body_buf, sizeof(body_buf)));
}

} // namespace bytetaper::taperquery

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/yaml_loader.h"
#include "runtime/policy_snapshot.h"

#include <gtest/gtest.h>

namespace bytetaper::runtime {

TEST(RuntimePolicySnapshotTest, BuildFromRoutesArray) {
    policy::RoutePolicy raw_routes[2]{};
    raw_routes[0].route_id = "route-1";
    raw_routes[0].match_prefix = "/v1";
    raw_routes[1].route_id = "route-2";
    raw_routes[1].match_prefix = "/v2";

    auto build_res = build_runtime_policy_snapshot_from_routes(raw_routes, 2, "test_file.yaml", 42);
    ASSERT_TRUE(build_res.ok);
    ASSERT_NE(build_res.snapshot, nullptr);

    const auto& snapshot = build_res.snapshot;
    EXPECT_EQ(snapshot->source_name, "test_file.yaml");
    EXPECT_EQ(snapshot->generation, 42);
    ASSERT_EQ(snapshot->routes.size(), 2);

    // Verify string ownership & pointer safety
    EXPECT_STREQ(snapshot->routes[0].route_id, "route-1");
    EXPECT_STREQ(snapshot->routes[0].match_prefix, "/v1");
    EXPECT_STREQ(snapshot->routes[1].route_id, "route-2");
    EXPECT_STREQ(snapshot->routes[1].match_prefix, "/v2");

    // The pointers must point inside the snapshot's own memory (the policy_ir strings)
    // to guarantee hot-reload reference-counted safety.
    EXPECT_EQ(snapshot->routes[0].route_id, snapshot->policy_ir.routes[0].route_id.c_str());
    EXPECT_EQ(snapshot->routes[0].match_prefix, snapshot->policy_ir.routes[0].match_prefix.c_str());

    // Verify route matcher and runtime table were compiled
    EXPECT_TRUE(snapshot->route_matcher_ready);
    EXPECT_EQ(snapshot->route_runtimes.count, 2);
    EXPECT_FALSE(snapshot->policy_identity.empty());
    EXPECT_NE(snapshot->policy_identity, "unknown");
}

TEST(RuntimePolicySnapshotTest, BuildFromEmptyRoutes) {
    auto build_res = build_runtime_policy_snapshot_from_routes(nullptr, 0, "empty.yaml", 1);
    ASSERT_TRUE(build_res.ok);
    ASSERT_NE(build_res.snapshot, nullptr);
    EXPECT_TRUE(build_res.snapshot->routes.empty());
    EXPECT_FALSE(build_res.snapshot->route_matcher_ready);
}

TEST(RuntimePolicySnapshotTest, BuildFromTqPolicyDocument) {
    taperquery::TqPolicyDocument doc{};
    doc.source_name = "tq_doc.tq";

    taperquery::TqRoutePolicy rp1{};
    rp1.route_id = "r-tq-1";
    rp1.match_prefix = "/tq1";
    doc.routes.push_back(rp1);

    auto build_res = build_runtime_policy_snapshot_from_ir(doc, 100);
    ASSERT_TRUE(build_res.ok);
    ASSERT_NE(build_res.snapshot, nullptr);

    const auto& snapshot = build_res.snapshot;
    EXPECT_EQ(snapshot->source_name, "tq_doc.tq");
    EXPECT_EQ(snapshot->generation, 100);
    ASSERT_EQ(snapshot->routes.size(), 1);
    EXPECT_STREQ(snapshot->routes[0].route_id, "r-tq-1");
    EXPECT_STREQ(snapshot->routes[0].match_prefix, "/tq1");

    // Pointer-safety check:
    EXPECT_EQ(snapshot->routes[0].route_id, snapshot->policy_ir.routes[0].route_id.c_str());
}

TEST(RuntimePolicySnapshotTest, SnapshotOwnsDataAfterSourceOutOfScope) {
    std::shared_ptr<const RuntimePolicySnapshot> snapshot;
    {
        policy::RoutePolicy raw_routes[1]{};
        raw_routes[0].route_id = "temp-route";
        raw_routes[0].match_prefix = "/temp";
        auto build_res = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "temp.yaml", 10);
        ASSERT_TRUE(build_res.ok);
        snapshot = build_res.snapshot;
    }
    // raw_routes is out of scope, but snapshot must own its data perfectly
    EXPECT_STREQ(snapshot->routes[0].route_id, "temp-route");
    EXPECT_STREQ(snapshot->routes[0].match_prefix, "/temp");
}

TEST(RuntimePolicySnapshotTest, RouteMatcherPointsIntoSnapshotRoutes) {
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "matcher-route";
    raw_routes[0].match_prefix = "/match";
    auto build_res = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "matcher.yaml", 11);
    ASSERT_TRUE(build_res.ok);
    const auto& snapshot = build_res.snapshot;

    const policy::RoutePolicy* matched =
        policy::match_route_compiled(snapshot->route_matcher, "/match/foo", nullptr);
    ASSERT_NE(matched, nullptr);
    // matched ptr must be exactly inside snapshot->routes
    EXPECT_EQ(matched, &snapshot->routes[0]);
}

TEST(RuntimePolicySnapshotTest, RouteRuntimeTableBelongsToSameSnapshot) {
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "runtime-route";
    raw_routes[0].match_prefix = "/run";
    auto build_res = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "runtime.yaml", 12);
    ASSERT_TRUE(build_res.ok);
    const auto& snapshot = build_res.snapshot;

    auto compiled =
        extproc::find_compiled_route_runtime(snapshot->route_runtimes, &snapshot->routes[0]);
    ASSERT_NE(compiled, nullptr);
    EXPECT_STREQ(compiled->policy->route_id, "runtime-route");
}

TEST(RuntimePolicySnapshotTest, SnapshotIdentityIsStable) {
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "stable-route";
    raw_routes[0].match_prefix = "/stable";

    auto res1 = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "stable.yaml", 1);
    auto res2 = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "stable.yaml", 2);

    ASSERT_TRUE(res1.ok);
    ASSERT_TRUE(res2.ok);
    EXPECT_FALSE(res1.snapshot->policy_identity.empty());
    EXPECT_EQ(res1.snapshot->policy_identity, res2.snapshot->policy_identity);
}

TEST(RuntimePolicySnapshotTest, SnapshotGenerationPreserved) {
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "gen-route";
    raw_routes[0].match_prefix = "/gen";
    auto res = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "gen.yaml", 999);
    ASSERT_TRUE(res.ok);
    EXPECT_EQ(res.snapshot->generation, 999u);
}

TEST(RuntimePolicySnapshotTest, ConvertsCacheInvalidationPolicy) {
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "inv-route";
    raw_routes[0].match_prefix = "/inv";
    raw_routes[0].cache.invalidation.enabled = true;
    raw_routes[0].cache.invalidation.on_patch = true;
    raw_routes[0].cache.invalidation.on_put = true;
    raw_routes[0].cache.invalidation.timing =
        policy::CacheInvalidationTiming::AfterSuccessfulUpstreamResponse;
    raw_routes[0].cache.invalidation.success_status_min = 200;
    raw_routes[0].cache.invalidation.success_status_max = 299;
    raw_routes[0].cache.invalidation.target_count = 1;
    std::strncpy(raw_routes[0].cache.invalidation.targets[0].route_id, "target",
                 policy::kMaxRouteIdLen - 1);
    raw_routes[0].cache.invalidation.targets[0].strategy =
        policy::CacheInvalidationStrategy::RouteEpoch;

    auto build_res = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "inv.yaml", 100);
    ASSERT_TRUE(build_res.ok);
    const auto& snapshot = build_res.snapshot;

    // Check runtime to TQ conversion (in policy_ir)
    const auto& tq_route = snapshot->policy_ir.routes[0];
    EXPECT_TRUE(tq_route.cache.invalidation.enabled);
    EXPECT_EQ(tq_route.cache.invalidation.timing, "after_successful_upstream_response");
    EXPECT_EQ(tq_route.cache.invalidation.success_status_min, 200);
    EXPECT_EQ(tq_route.cache.invalidation.success_status_max, 299);
    ASSERT_EQ(tq_route.cache.invalidation.on_methods.size(), 2u);
    EXPECT_EQ(tq_route.cache.invalidation.on_methods[0], "PATCH");
    EXPECT_EQ(tq_route.cache.invalidation.on_methods[1], "PUT");
    ASSERT_EQ(tq_route.cache.invalidation.targets.size(), 1u);
    EXPECT_EQ(tq_route.cache.invalidation.targets[0].route_id, "target");
    EXPECT_EQ(tq_route.cache.invalidation.targets[0].strategy,
              taperquery::TqCacheInvalidationStrategy::RouteEpoch);

    // Check TQ to runtime conversion (in routes)
    const auto& rt_route = snapshot->routes[0];
    EXPECT_TRUE(rt_route.cache.invalidation.enabled);
    EXPECT_TRUE(rt_route.cache.invalidation.on_patch);
    EXPECT_TRUE(rt_route.cache.invalidation.on_put);
    EXPECT_FALSE(rt_route.cache.invalidation.on_delete);
    EXPECT_EQ(rt_route.cache.invalidation.timing,
              policy::CacheInvalidationTiming::AfterSuccessfulUpstreamResponse);
    EXPECT_EQ(rt_route.cache.invalidation.success_status_min, 200);
    EXPECT_EQ(rt_route.cache.invalidation.success_status_max, 299);
    EXPECT_EQ(rt_route.cache.invalidation.target_count, 1u);
    EXPECT_STREQ(rt_route.cache.invalidation.targets[0].route_id, "target");
    EXPECT_EQ(rt_route.cache.invalidation.targets[0].strategy,
              policy::CacheInvalidationStrategy::RouteEpoch);
}

} // namespace bytetaper::runtime

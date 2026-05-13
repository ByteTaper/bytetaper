// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

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

} // namespace bytetaper::runtime

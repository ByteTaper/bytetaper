// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/route_matcher.h"
#include "policy/route_policy.h"

#include <gtest/gtest.h>

namespace bytetaper::policy {

TEST(RouteMatcherTest, PrefixMatchReturnsFirstPolicy) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/v1/";

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/api/v1/users");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "p1");
}

TEST(RouteMatcherTest, PrefixMatchWithTrailingSlash) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/";

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/api/");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "p1");
}

TEST(RouteMatcherTest, ExactMatchReturnsPolicy) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "e1";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[0].match_prefix = "/health";

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/health");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "e1");
}

TEST(RouteMatcherTest, ExactMatchDoesNotMatchSubpath) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "e1";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[0].match_prefix = "/health";

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/health/live");
    EXPECT_EQ(matched, nullptr);
}

TEST(RouteMatcherTest, PrefixMatchDoesNotMatchShorterPath) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/v1/";

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/api");
    EXPECT_EQ(matched, nullptr);
}

TEST(RouteMatcherTest, FirstMatchWins) {
    RoutePolicy policies[2] = {};
    policies[0].route_id = "first";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/";

    policies[1].route_id = "second";
    policies[1].match_kind = RouteMatchKind::Prefix;
    policies[1].match_prefix = "/api/v1/";

    // Both match, but the first one in the array should win.
    const RoutePolicy* matched = match_route_by_path(policies, 2, "/api/v1/users");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "first");
}

TEST(RouteMatcherTest, NoMatchReturnsNullptr) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/";

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/unknown");
    EXPECT_EQ(matched, nullptr);
}

TEST(RouteMatcherTest, NullPathReturnsNullptr) {
    RoutePolicy policies[1] = {};
    policies[0].match_prefix = "/";
    EXPECT_EQ(match_route_by_path(policies, 1, nullptr), nullptr);
}

TEST(RouteMatcherTest, EmptyPoliciesReturnsNullptr) {
    EXPECT_EQ(match_route_by_path(nullptr, 0, "/api"), nullptr);
}

TEST(RouteMatcherTest, ExactMatchReturnsMutationMode) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "e1";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[0].match_prefix = "/health";
    policies[0].mutation = MutationMode::HeadersOnly;

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/health");
    ASSERT_NE(matched, nullptr);
    EXPECT_EQ(matched->mutation, MutationMode::HeadersOnly);
}

TEST(RouteMatcherTest, PrefixMatchEmptyPrefix) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "catch-all";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "";

    const RoutePolicy* matched = match_route_by_path(policies, 1, "/anything");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "catch-all");
}

TEST(CompiledRouteMatcherTest, CompiledPrefixMatch) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/v1/";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 1, &matcher);

    const RoutePolicy* matched = match_route_compiled(matcher, "/api/v1/users");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "p1");
}

TEST(CompiledRouteMatcherTest, CompiledExactMatch) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "e1";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[0].match_prefix = "/health";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 1, &matcher);

    // Exact match matches the same path
    const RoutePolicy* matched1 = match_route_compiled(matcher, "/health");
    ASSERT_NE(matched1, nullptr);
    EXPECT_STREQ(matched1->route_id, "e1");

    // Exact match does not match subpath
    const RoutePolicy* matched2 = match_route_compiled(matcher, "/health/live");
    EXPECT_EQ(matched2, nullptr);
}

TEST(CompiledRouteMatcherTest, CompiledFirstMatchWins_PrefixBeforeExact) {
    RoutePolicy policies[2] = {};
    policies[0].route_id = "p_first";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/";

    policies[1].route_id = "e_second";
    policies[1].match_kind = RouteMatchKind::Exact;
    policies[1].match_prefix = "/api/v1/users";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 2, &matcher);

    // Request path /api/v1/users matches both policies, but prefix is first in order so it wins
    const RoutePolicy* matched = match_route_compiled(matcher, "/api/v1/users");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "p_first");
}

TEST(CompiledRouteMatcherTest, CompiledFirstMatchWins_ExactBeforePrefix) {
    RoutePolicy policies[2] = {};
    policies[0].route_id = "e_first";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[0].match_prefix = "/api/v1/users";

    policies[1].route_id = "p_second";
    policies[1].match_kind = RouteMatchKind::Prefix;
    policies[1].match_prefix = "/api/";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 2, &matcher);

    // Request path /api/v1/users matches both policies, but exact is first in order so it wins
    const RoutePolicy* matched = match_route_compiled(matcher, "/api/v1/users");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "e_first");
}

TEST(CompiledRouteMatcherTest, CompiledNullPathReturnsNullptr) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 1, &matcher);

    EXPECT_EQ(match_route_compiled(matcher, nullptr), nullptr);
}

TEST(CompiledRouteMatcherTest, CompiledNullPrefixEntrySkipped) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = nullptr;

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 1, &matcher);

    EXPECT_EQ(matcher.exact_count, 0u);
    EXPECT_EQ(matcher.prefix_count, 0u);
}

TEST(CompiledRouteMatcherTest, CompiledPrefixLengthPrecomputed) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/v1/";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 1, &matcher);

    ASSERT_EQ(matcher.prefix_count, 1u);
    EXPECT_EQ(matcher.prefix_routes[0].prefix_len, 8u);
    EXPECT_STREQ(matcher.prefix_routes[0].prefix, "/api/v1/");
}

TEST(CompiledRouteMatcherTest, CompiledMatcherEqualsRawMatcher) {
    RoutePolicy policies[4] = {};
    policies[0].route_id = "e1";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[0].match_prefix = "/api/v1/users";

    policies[1].route_id = "p1";
    policies[1].match_kind = RouteMatchKind::Prefix;
    policies[1].match_prefix = "/api/v1/";

    policies[2].route_id = "e2";
    policies[2].match_kind = RouteMatchKind::Exact;
    policies[2].match_prefix = "/api/v2/users";

    policies[3].route_id = "p2";
    policies[3].match_kind = RouteMatchKind::Prefix;
    policies[3].match_prefix = "/api/";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 4, &matcher);

    const char* paths[] = { "/api/v1/users", "/api/v1/users/profile",
                            "/api/v2/users", "/api/v2/users/settings",
                            "/api/other",    "/outside",
                            nullptr };

    for (const char* path : paths) {
        const RoutePolicy* raw_res = match_route_by_path(policies, 4, path);
        const RoutePolicy* comp_res = match_route_compiled(matcher, path);
        EXPECT_EQ(raw_res, comp_res) << "Mismatch on path: " << (path ? path : "nullptr");
    }
}

TEST(CompiledRouteMatcherTest, MultiplePrefixes_FirstWins) {
    RoutePolicy policies[2] = {};
    policies[0].route_id = "p_short";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/";

    policies[1].route_id = "p_long";
    policies[1].match_kind = RouteMatchKind::Prefix;
    policies[1].match_prefix = "/api/v1/";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 2, &matcher);

    // /api/v1/users matches both, but short is first so it wins
    const RoutePolicy* matched = match_route_compiled(matcher, "/api/v1/users");
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "p_short");
}

TEST(CompiledRouteMatcherTest, NoMatch_ReturnsNullptr) {
    RoutePolicy policies[1] = {};
    policies[0].route_id = "p1";
    policies[0].match_kind = RouteMatchKind::Prefix;
    policies[0].match_prefix = "/api/";

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 1, &matcher);

    EXPECT_EQ(match_route_compiled(matcher, "/unknown"), nullptr);
}

TEST(CompiledRouteMatcherTest, HashStrategyMatchesLinearResult) {
    RoutePolicy policies[10] = {};
    policies[0].route_id = "r0";
    policies[0].match_prefix = "/path0";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[1].route_id = "r1";
    policies[1].match_prefix = "/path1";
    policies[1].match_kind = RouteMatchKind::Exact;
    policies[2].route_id = "r2";
    policies[2].match_prefix = "/path2";
    policies[2].match_kind = RouteMatchKind::Exact;
    policies[3].route_id = "r3";
    policies[3].match_prefix = "/path3";
    policies[3].match_kind = RouteMatchKind::Exact;
    policies[4].route_id = "r4";
    policies[4].match_prefix = "/path4";
    policies[4].match_kind = RouteMatchKind::Exact;
    policies[5].route_id = "r5";
    policies[5].match_prefix = "/path5";
    policies[5].match_kind = RouteMatchKind::Exact;
    policies[6].route_id = "r6";
    policies[6].match_prefix = "/path6";
    policies[6].match_kind = RouteMatchKind::Exact;
    policies[7].route_id = "r7";
    policies[7].match_prefix = "/path7";
    policies[7].match_kind = RouteMatchKind::Exact;
    policies[8].route_id = "r8";
    policies[8].match_prefix = "/path8";
    policies[8].match_kind = RouteMatchKind::Exact;
    policies[9].route_id = "r9";
    policies[9].match_prefix = "/path9";
    policies[9].match_kind = RouteMatchKind::Exact;

    CompiledRouteMatcher linear_matcher;
    compile_route_matcher(policies, 10, &linear_matcher, RouteMatcherStrategy::LinearSmall);

    CompiledRouteMatcher hash_matcher;
    compile_route_matcher(policies, 10, &hash_matcher, RouteMatcherStrategy::ExactHashPrefixLinear);

    EXPECT_EQ(linear_matcher.strategy, RouteMatcherStrategy::LinearSmall);
    EXPECT_EQ(hash_matcher.strategy, RouteMatcherStrategy::ExactHashPrefixLinear);

    const char* paths[] = { "/path0", "/path5", "/path9", "/path_unknown", nullptr };
    for (const char* path : paths) {
        if (path == nullptr)
            continue;
        const RoutePolicy* linear_res = match_route_compiled(linear_matcher, path);
        const RoutePolicy* hash_res = match_route_compiled(hash_matcher, path);
        EXPECT_EQ(linear_res, hash_res) << "Mismatch on path: " << path;
    }
}

TEST(CompiledRouteMatcherTest, HashStrategyExactHitMetric) {
    RoutePolicy policies[10] = {};
    policies[0].route_id = "r0";
    policies[0].match_prefix = "/path0";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[1].route_id = "r1";
    policies[1].match_prefix = "/path1";
    policies[1].match_kind = RouteMatchKind::Exact;
    policies[2].route_id = "r2";
    policies[2].match_prefix = "/path2";
    policies[2].match_kind = RouteMatchKind::Exact;
    policies[3].route_id = "r3";
    policies[3].match_prefix = "/path3";
    policies[3].match_kind = RouteMatchKind::Exact;
    policies[4].route_id = "r4";
    policies[4].match_prefix = "/path4";
    policies[4].match_kind = RouteMatchKind::Exact;
    policies[5].route_id = "r5";
    policies[5].match_prefix = "/path5";
    policies[5].match_kind = RouteMatchKind::Exact;
    policies[6].route_id = "r6";
    policies[6].match_prefix = "/path6";
    policies[6].match_kind = RouteMatchKind::Exact;
    policies[7].route_id = "r7";
    policies[7].match_prefix = "/path7";
    policies[7].match_kind = RouteMatchKind::Exact;
    policies[8].route_id = "r8";
    policies[8].match_prefix = "/path8";
    policies[8].match_kind = RouteMatchKind::Exact;
    policies[9].route_id = "r9";
    policies[9].match_prefix = "/path9";
    policies[9].match_kind = RouteMatchKind::Exact;

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 10, &matcher);
    EXPECT_EQ(matcher.strategy, RouteMatcherStrategy::ExactHashPrefixLinear);

    metrics::RuntimeMetrics m{};
    const RoutePolicy* matched = match_route_compiled(matcher, "/path5", &m);
    ASSERT_NE(matched, nullptr);
    EXPECT_STREQ(matched->route_id, "r5");

    EXPECT_EQ(m.route_match_exact_hash_hit_total.load(), 1u);
    EXPECT_EQ(m.route_match_exact_scan_total.load(), 0u);
    EXPECT_EQ(m.route_match_prefix_scan_total.load(), 0u);
    EXPECT_EQ(m.route_match_no_match_total.load(), 0u);
}

TEST(CompiledRouteMatcherTest, HashStrategyNoMatchMetric) {
    RoutePolicy policies[10] = {};
    policies[0].route_id = "r0";
    policies[0].match_prefix = "/path0";
    policies[0].match_kind = RouteMatchKind::Exact;
    policies[1].route_id = "r1";
    policies[1].match_prefix = "/path1";
    policies[1].match_kind = RouteMatchKind::Exact;
    policies[2].route_id = "r2";
    policies[2].match_prefix = "/path2";
    policies[2].match_kind = RouteMatchKind::Exact;
    policies[3].route_id = "r3";
    policies[3].match_prefix = "/path3";
    policies[3].match_kind = RouteMatchKind::Exact;
    policies[4].route_id = "r4";
    policies[4].match_prefix = "/path4";
    policies[4].match_kind = RouteMatchKind::Exact;
    policies[5].route_id = "r5";
    policies[5].match_prefix = "/path5";
    policies[5].match_kind = RouteMatchKind::Exact;
    policies[6].route_id = "r6";
    policies[6].match_prefix = "/path6";
    policies[6].match_kind = RouteMatchKind::Exact;
    policies[7].route_id = "r7";
    policies[7].match_prefix = "/path7";
    policies[7].match_kind = RouteMatchKind::Exact;
    policies[8].route_id = "r8";
    policies[8].match_prefix = "/path8";
    policies[8].match_kind = RouteMatchKind::Exact;
    policies[9].route_id = "r9";
    policies[9].match_prefix = "/path9";
    policies[9].match_kind = RouteMatchKind::Exact;

    CompiledRouteMatcher matcher;
    compile_route_matcher(policies, 10, &matcher);

    metrics::RuntimeMetrics m{};
    const RoutePolicy* matched = match_route_compiled(matcher, "/unknown_path", &m);
    EXPECT_EQ(matched, nullptr);

    EXPECT_EQ(m.route_match_no_match_total.load(), 1u);
    EXPECT_EQ(m.route_match_exact_hash_hit_total.load(), 0u);
}

} // namespace bytetaper::policy

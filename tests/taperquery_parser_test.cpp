// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_parser.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::taperquery {

TEST(TqParserTest, ParsesEmptyDocument) {
    const char* src = "";
    TqParseResult res = parse_taperquery_source(src, 0);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.document.policies.size(), 0u);
    EXPECT_EQ(res.document.top_level_routes.size(), 0u);
}

TEST(TqParserTest, ParsesPolicyBlock) {
    const char* src = "policy \"my_policy\" schema \"v1\" against sha \"abc\" {}";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.document.policies.size(), 1u);
    EXPECT_EQ(res.document.policies[0].name, "my_policy");
    EXPECT_EQ(res.document.policies[0].schema, "v1");
    EXPECT_EQ(res.document.policies[0].sha256, "abc");
}

TEST(TqParserTest, ParsesTopLevelRoute) {
    const char* src = "route \"simple_route\" {}";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.document.top_level_routes.size(), 1u);
    EXPECT_EQ(res.document.top_level_routes[0].name, "simple_route");
}

TEST(TqParserTest, ParsesRouteWithPrefixMatch) {
    const char* src = "route \"r1\" when path prefix \"/api/v1/\" {}";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.document.top_level_routes.size(), 1u);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    EXPECT_TRUE(r.has_when);
    EXPECT_EQ(r.when_clause.match_expr.path_kind, "prefix");
    EXPECT_EQ(r.when_clause.match_expr.path, "/api/v1/");
}

TEST(TqParserTest, ParsesRouteWithExactMatch) {
    const char* src = "route \"r1\" when path exact \"/api/v1\" {}";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.document.top_level_routes.size(), 1u);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    EXPECT_TRUE(r.has_when);
    EXPECT_EQ(r.when_clause.match_expr.path_kind, "exact");
    EXPECT_EQ(r.when_clause.match_expr.path, "/api/v1");
}

TEST(TqParserTest, ParsesRouteWithMethodAndPath) {
    const char* src = "route \"r1\" when method get and path prefix \"/api/\" {}";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.document.top_level_routes.size(), 1u);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    EXPECT_TRUE(r.has_when);
    EXPECT_EQ(r.when_clause.match_expr.method, "get");
    EXPECT_EQ(r.when_clause.match_expr.path_kind, "prefix");
    EXPECT_EQ(r.when_clause.match_expr.path, "/api/");
}

TEST(TqParserTest, ParsesRouteWithAlternativeWhenOrder) {
    const char* src = "route \"r1\" when path prefix \"/api/\" method get {}";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.document.top_level_routes.size(), 1u);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    EXPECT_TRUE(r.has_when);
    EXPECT_EQ(r.when_clause.match_expr.method, "get");
    EXPECT_EQ(r.when_clause.match_expr.path_kind, "prefix");
    EXPECT_EQ(r.when_clause.match_expr.path, "/api/");
}

TEST(TqParserTest, ParsesMutationStatement) {
    const char* src = "route \"r1\" { mutate full; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    ASSERT_EQ(r.statements.size(), 1u);
    EXPECT_EQ(r.statements[0].kind, TqAstStatementKind::Mutate);
    EXPECT_EQ(r.statements[0].mutate.mode, "full");
}

TEST(TqParserTest, ParsesFailureStatement) {
    const char* src = "route \"r1\" { failure fail_closed; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    ASSERT_EQ(r.statements.size(), 1u);
    EXPECT_EQ(r.statements[0].kind, TqAstStatementKind::Failure);
    EXPECT_EQ(r.statements[0].failure.mode, "fail_closed");
}

TEST(TqParserTest, ParsesMaxResponseStatement) {
    const char* src = "route \"r1\" { max_response 2MiB; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    ASSERT_EQ(r.statements.size(), 1u);
    EXPECT_EQ(r.statements[0].kind, TqAstStatementKind::MaxResponse);
    EXPECT_EQ(r.statements[0].max_response.limit_bytes, 2097152u);
}

TEST(TqParserTest, ParsesCacheStoreTtl) {
    const char* src = "route \"r1\" { cache store ttl 300s; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    ASSERT_EQ(r.statements.size(), 1u);
    EXPECT_EQ(r.statements[0].kind, TqAstStatementKind::Cache);
    EXPECT_TRUE(r.statements[0].cache.enabled);
    EXPECT_EQ(r.statements[0].cache.behavior, "store");
    EXPECT_EQ(r.statements[0].cache.ttl_ms, 300000u);
}

TEST(TqParserTest, ParsesCacheStoreTtlMsPrecision) {
    const char* src = "route \"r1\" { cache store ttl 250ms; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    ASSERT_EQ(r.statements.size(), 1u);
    EXPECT_EQ(r.statements[0].kind, TqAstStatementKind::Cache);
    EXPECT_TRUE(r.statements[0].cache.enabled);
    EXPECT_EQ(r.statements[0].cache.behavior, "store");
    EXPECT_EQ(r.statements[0].cache.ttl_ms, 250u);
}

TEST(TqParserTest, ParsesCacheL1Block) {
    const char* src = "route \"r1\" { cache { l1 enabled capacity 5000 entries } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCacheStmt cache = r.statements[0].cache;
    EXPECT_TRUE(cache.l1.enabled);
    EXPECT_EQ(cache.l1.capacity_entries, 5000u);
}

TEST(TqParserTest, ParsesCacheL2Block) {
    const char* src = "route \"r1\" { cache { l2 enabled path \"/var/lib/cache\" } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCacheStmt cache = r.statements[0].cache;
    EXPECT_TRUE(cache.l2.enabled);
    EXPECT_EQ(cache.l2.path, "/var/lib/cache");
}

TEST(TqParserTest, ParsesPrivateCacheClause) {
    const char* src = "route \"r1\" { cache { private auth_scope_header \"x-user-id\" } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCacheStmt cache = r.statements[0].cache;
    EXPECT_TRUE(cache.private_cache.enabled);
    EXPECT_EQ(cache.private_cache.auth_scope_header, "x-user-id");
}

TEST(TqParserTest, ParsesVaryHeadersClause) {
    const char* src =
        "route \"r1\" { cache { vary by headers [\"accept-encoding\", \"user-agent\"] } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCacheStmt cache = r.statements[0].cache;
    ASSERT_EQ(cache.vary_headers.headers.size(), 2u);
    EXPECT_EQ(cache.vary_headers.headers[0], "accept-encoding");
    EXPECT_EQ(cache.vary_headers.headers[1], "user-agent");
}

TEST(TqParserTest, ParsesFieldVariantClause) {
    const char* src =
        "route \"r1\" { cache { field_variant { enabled true max_variants_per_route 100 "
        "min_field_count 1 max_field_count 10 admission_threshold 5 ttl_max 120s } } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCacheStmt cache = r.statements[0].cache;
    EXPECT_TRUE(cache.field_variant.enabled);
    EXPECT_EQ(cache.field_variant.max_variants_per_route, 100u);
    EXPECT_EQ(cache.field_variant.min_field_count, 1u);
    EXPECT_EQ(cache.field_variant.max_field_count, 10u);
    EXPECT_EQ(cache.field_variant.admission_threshold, 5u);
    EXPECT_EQ(cache.field_variant.ttl_max_ms, 120000u);
}

TEST(TqParserTest, ParsesFieldsAllowListWithIdentifiers) {
    const char* src = "route \"r1\" { fields allow [id, name]; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstFieldsStmt fields = r.statements[0].fields;
    EXPECT_EQ(fields.mode, "allow");
    ASSERT_EQ(fields.fields.size(), 2u);
    EXPECT_EQ(fields.fields[0], "id");
    EXPECT_EQ(fields.fields[1], "name");
}

TEST(TqParserTest, ParsesFieldsDenyList) {
    const char* src = "route \"r1\" { fields deny [\"password\"]; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstFieldsStmt fields = r.statements[0].fields;
    EXPECT_EQ(fields.mode, "deny");
    ASSERT_EQ(fields.fields.size(), 1u);
    EXPECT_EQ(fields.fields[0], "password");
}

TEST(TqParserTest, ParsesPaginationLimitOffsetBlock) {
    const char* src = "route \"r1\" { paginate limit_offset { limit_param \"limit\" offset_param "
                      "\"offset\" default_limit 20 max_limit 100 upstream_supports_pagination true "
                      "max_response_bytes_warning 512KiB } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstPaginateStmt pag = r.statements[0].paginate;
    EXPECT_TRUE(pag.enabled);
    EXPECT_EQ(pag.mode, "limit_offset");
    EXPECT_EQ(pag.limit_param, "limit");
    EXPECT_EQ(pag.offset_param, "offset");
    EXPECT_EQ(pag.default_limit, 20u);
    EXPECT_EQ(pag.max_limit, 100u);
    EXPECT_TRUE(pag.upstream_supports_pagination);
    EXPECT_EQ(pag.max_response_bytes_warning, 524288u);
}

TEST(TqParserTest, ParsesCompressionBlock) {
    const char* src = "route \"r1\" { compress { min_size 1024B eligible_content_types "
                      "[\"application/json\"] prefer [gzip, brotli] already_encoded skip } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCompressStmt comp = r.statements[0].compress;
    EXPECT_TRUE(comp.enabled);
    EXPECT_EQ(comp.min_size_bytes, 1024u);
    ASSERT_EQ(comp.eligible_content_types.size(), 1u);
    EXPECT_EQ(comp.eligible_content_types[0], "application/json");
    ASSERT_EQ(comp.preferred_algorithms.size(), 2u);
    EXPECT_EQ(comp.preferred_algorithms[0], "gzip");
    EXPECT_EQ(comp.preferred_algorithms[1], "brotli");
    EXPECT_EQ(comp.already_encoded_behavior, "skip");
}

TEST(TqParserTest, ParsesCoalescingBlock) {
    const char* src = "route \"r1\" { coalesce cache_assisted { backend_timeout 5s handoff_buffer "
                      "100ms result_ready_retention 200ms max_waiters_per_key 50 require_cache "
                      "true allow_authenticated false } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCoalesceStmt coal = r.statements[0].coalesce;
    EXPECT_TRUE(coal.enabled);
    EXPECT_EQ(coal.mode, "cache_assisted");
    EXPECT_EQ(coal.backend_timeout_ms, 5000u);
    EXPECT_EQ(coal.handoff_buffer_ms, 100u);
    EXPECT_EQ(coal.result_ready_retention_ms, 200u);
    EXPECT_EQ(coal.max_waiters_per_key, 50u);
    EXPECT_TRUE(coal.require_cache_enabled);
    EXPECT_FALSE(coal.allow_authenticated);
}

TEST(TqParserTest, ParsesGuardrailsBlock) {
    const char* src =
        "route \"r1\" { coalesce cache_assisted { guardrails { max_follower_wait_budget 1s "
        "max_active_follower_waiters 200 max_active_follower_waiters_per_shard 50 } } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCoalesceStmt coal = r.statements[0].coalesce;
    EXPECT_TRUE(coal.guardrails.enabled);
    EXPECT_EQ(coal.guardrails.max_follower_wait_budget_ms, 1000u);
    EXPECT_EQ(coal.guardrails.max_active_follower_waiters, 200u);
    EXPECT_EQ(coal.guardrails.max_active_follower_waiters_per_shard, 50u);
}

TEST(TqParserTest, ParsesCompletePolicyExample) {
    const char* src = R"(
policy "complete_bytetaper_policy" schema "v2" against sha "abcdef" {
    route "api_v1_route" when method get and path prefix "/api/v1" {
        mutate full;
        failure fail_open;
        max_response 10MiB;
        cache store ttl 60s {
            l1 enabled capacity 1000 entries
            l2 path "/tmp/cache"
        }
        fields allow [id, username];
    }
}
)";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    ASSERT_EQ(res.document.policies.size(), 1u);
    TqAstPolicyDecl pol = res.document.policies[0];
    EXPECT_EQ(pol.name, "complete_bytetaper_policy");
    ASSERT_EQ(pol.routes.size(), 1u);
    TqAstRouteDecl r = pol.routes[0];
    EXPECT_EQ(r.name, "api_v1_route");
    EXPECT_TRUE(r.has_when);
    EXPECT_EQ(r.when_clause.match_expr.method, "get");
    EXPECT_EQ(r.when_clause.match_expr.path, "/api/v1");
    EXPECT_EQ(r.statements.size(), 5u);
}

TEST(TqParserTest, ParsesCacheInvalidationCanonical) {
    const char* src = R"(
route "r1" {
    cache invalidation {
        enabled true
        on_methods ["PATCH", "PUT"]
        timing "after_successful_upstream_response"
        success_status min 200 max 299
        target "get_user" {
            strategy "route_epoch"
        }
    }
}
)";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    ASSERT_EQ(r.statements.size(), 1u);
    EXPECT_EQ(r.statements[0].kind, TqAstStatementKind::Cache);
    TqAstCacheStmt cache = r.statements[0].cache;
    EXPECT_TRUE(cache.invalidation.enabled);
    ASSERT_EQ(cache.invalidation.on_methods.size(), 2u);
    EXPECT_EQ(cache.invalidation.on_methods[0], "PATCH");
    EXPECT_EQ(cache.invalidation.on_methods[1], "PUT");
    EXPECT_EQ(cache.invalidation.timing, "after_successful_upstream_response");
    EXPECT_EQ(cache.invalidation.success_status_min, 200);
    EXPECT_EQ(cache.invalidation.success_status_max, 299);
    ASSERT_EQ(cache.invalidation.targets.size(), 1u);
    EXPECT_EQ(cache.invalidation.targets[0].route_id, "get_user");
    EXPECT_EQ(cache.invalidation.targets[0].strategy, "route_epoch");
}

TEST(TqParserTest, ParsesCaseInsensitiveMethods) {
    const char* cases[] = { "route \"r1\" when method GET and path prefix \"/\" {}",
                            "route \"r2\" when method Patch and path prefix \"/\" {}",
                            "route \"r3\" when method deLEte and path prefix \"/\" {}",
                            "route \"r4\" when method ANY and path prefix \"/\" {}" };
    const char* expected[] = { "get", "patch", "delete", "any" };

    for (int i = 0; i < 4; ++i) {
        TqParseResult res = parse_taperquery_source(cases[i], std::strlen(cases[i]));
        ASSERT_TRUE(res.ok) << "Failed on case: " << cases[i];
        EXPECT_EQ(res.document.top_level_routes[0].when_clause.match_expr.method, expected[i]);
    }
}

TEST(TqParserTest, ParsesCacheInvalidationLegacy) {
    const char* src = R"(
route "r1" {
    cache {
        invalidation {
            enabled true
            on_methods ["DELETE"]
            target "get_user" { strategy "route_epoch" }
        }
    }
}
)";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    ASSERT_TRUE(res.ok);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    TqAstCacheStmt cache = r.statements[0].cache;
    EXPECT_TRUE(cache.invalidation.enabled);
    EXPECT_EQ(cache.invalidation.on_methods[0], "DELETE");
}

} // namespace bytetaper::taperquery

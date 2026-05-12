// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/policy_semantic_validator.h"
#include "policy/route_policy.h"
#include "taperquery/policy_ir_hash.h"
#include "taperquery/policy_ir_normalize.h"
#include "taperquery/policy_ir_printer.h"
#include "taperquery/tq_compiler.h"
#include "taperquery/tq_parser.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::taperquery {

namespace {

policy::RoutePolicy to_runtime_route_policy(const TqRoutePolicy& ir) {
    policy::RoutePolicy res{};
    res.route_id = ir.route_id.c_str();
    res.match_prefix = ir.match_prefix.c_str();
    res.match_kind = static_cast<policy::RouteMatchKind>(ir.match_kind);
    res.mutation = static_cast<policy::MutationMode>(ir.mutation);
    res.allowed_method = static_cast<policy::HttpMethod>(ir.allowed_method);

    res.field_filter.mode = static_cast<policy::FieldFilterMode>(ir.field_filter.mode);
    res.field_filter.field_count = std::min(ir.field_filter.fields.size(), policy::kMaxFields);
    for (std::size_t i = 0; i < res.field_filter.field_count; ++i) {
        std::strncpy(res.field_filter.fields[i], ir.field_filter.fields[i].c_str(),
                     policy::kMaxFieldNameLen - 1);
    }

    res.max_response_bytes = ir.max_response_bytes;

    res.cache.enabled = ir.cache.enabled;
    res.cache.behavior = static_cast<policy::CacheBehavior>(ir.cache.behavior);
    res.cache.ttl_seconds = ir.cache.ttl_ms / 1000u;
    res.cache.l1.enabled = ir.cache.l1.enabled;
    res.cache.l1.capacity_entries = ir.cache.l1.capacity_entries;
    res.cache.l2.enabled = ir.cache.l2.enabled;
    std::strncpy(res.cache.l2.path, ir.cache.l2.path.c_str(), policy::kMaxCachePathLen - 1);
    res.cache.private_cache = ir.cache.private_cache.enabled;
    std::strncpy(res.cache.auth_scope_header, ir.cache.private_cache.auth_scope_header.c_str(),
                 sizeof(res.cache.auth_scope_header) - 1);

    res.cache.field_variant.enabled = ir.cache.field_variant.enabled;
    res.cache.field_variant.max_variants_per_route = ir.cache.field_variant.max_variants_per_route;
    res.cache.field_variant.min_field_count = ir.cache.field_variant.min_field_count;
    res.cache.field_variant.max_field_count = ir.cache.field_variant.max_field_count;
    res.cache.field_variant.admission_threshold = ir.cache.field_variant.admission_threshold;
    res.cache.field_variant.ttl_max_ms = ir.cache.field_variant.ttl_max_ms;

    res.cache.vary_headers.count =
        std::min(ir.cache.vary_headers.names.size(), policy::kMaxCacheVaryHeaders);
    for (std::size_t i = 0; i < res.cache.vary_headers.count; ++i) {
        std::strncpy(res.cache.vary_headers.names[i], ir.cache.vary_headers.names[i].c_str(),
                     policy::kMaxCacheVaryHeaderNameLen - 1);
    }

    res.failure_mode = static_cast<policy::FailureMode>(ir.failure_mode);

    res.pagination.enabled = ir.pagination.enabled;
    res.pagination.mode = static_cast<policy::PaginationMode>(ir.pagination.mode);
    std::strncpy(res.pagination.limit_param, ir.pagination.limit_param.c_str(),
                 sizeof(res.pagination.limit_param) - 1);
    std::strncpy(res.pagination.offset_param, ir.pagination.offset_param.c_str(),
                 sizeof(res.pagination.offset_param) - 1);
    res.pagination.default_limit = ir.pagination.default_limit;
    res.pagination.max_limit = ir.pagination.max_limit;
    res.pagination.upstream_supports_pagination = ir.pagination.upstream_supports_pagination;
    res.pagination.max_response_bytes_warning = ir.pagination.max_response_bytes_warning;

    res.compression.enabled = ir.compression.enabled;
    res.compression.min_size_bytes = ir.compression.min_size_bytes;
    res.compression.eligible_content_type_count =
        std::min(ir.compression.eligible_content_types.size(), policy::kMaxEligibleContentTypes);
    for (std::size_t i = 0; i < res.compression.eligible_content_type_count; ++i) {
        std::strncpy(res.compression.eligible_content_types[i],
                     ir.compression.eligible_content_types[i].c_str(),
                     policy::kMaxContentTypeLen - 1);
    }
    res.compression.preferred_algorithm_count =
        std::min(ir.compression.preferred_algorithms.size(), policy::kMaxCompressionAlgorithms);
    for (std::size_t i = 0; i < res.compression.preferred_algorithm_count; ++i) {
        res.compression.preferred_algorithms[i] =
            static_cast<policy::CompressionAlgorithm>(ir.compression.preferred_algorithms[i]);
    }
    res.compression.already_encoded_behavior =
        static_cast<policy::AlreadyEncodedBehavior>(ir.compression.already_encoded_behavior);

    res.coalescing.enabled = ir.coalescing.enabled;
    res.coalescing.mode = static_cast<policy::CoalescingMode>(ir.coalescing.mode);
    res.coalescing.backend_timeout_ms = ir.coalescing.backend_timeout_ms;
    res.coalescing.handoff_buffer_ms = ir.coalescing.handoff_buffer_ms;
    res.coalescing.result_ready_retention_ms = ir.coalescing.result_ready_retention_ms;
    res.coalescing.max_waiters_per_key = ir.coalescing.max_waiters_per_key;
    res.coalescing.require_cache_enabled = ir.coalescing.require_cache_enabled;
    res.coalescing.allow_authenticated = ir.coalescing.allow_authenticated;
    res.coalescing.max_follower_wait_budget_ms = ir.coalescing.max_follower_wait_budget_ms;
    res.coalescing.max_active_follower_waiters = ir.coalescing.max_active_follower_waiters;
    res.coalescing.max_active_follower_waiters_per_shard =
        ir.coalescing.max_active_follower_waiters_per_shard;

    return res;
}

TqParseResult parse_src(const char* src) {
    return parse_taperquery_source(src, std::strlen(src));
}

} // namespace

// ==========================================
// 1. Document (5)
// ==========================================

TEST(TqCompilerTest, CompilesPolicyDocumentMetadata) {
    const char* src = "policy \"my_id\" schema \"v3\" against sha \"hash123\" {}";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_EQ(doc.document_id, "my_id");
    EXPECT_EQ(doc.schema_version, "v3");
    EXPECT_EQ(doc.expected_base_sha, "hash123");
}

TEST(TqCompilerTest, DefaultsSchemaVersionWhenMissing) {
    const char* src = "policy \"my_id\" {}";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_EQ(doc.schema_version, "tq/v1");
}

TEST(TqCompilerTest, RejectsMissingPolicyDeclarationInStrictMode) {
    const char* src = "route \"r1\" when path prefix \"/\" {}";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    opts.allow_partial_document = false;
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

TEST(TqCompilerTest, RejectsDuplicatePolicyMetadata) {
    // Parser itself won't parse two policies due to parser rules, but compiler must enforce
    TqAstDocument ast{};
    TqAstPolicyDecl p1{};
    p1.name = "id1";
    TqAstPolicyDecl p2{};
    p2.name = "id2";
    ast.policies.push_back(p1);
    ast.policies.push_back(p2);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

TEST(TqCompilerTest, RejectsDuplicateRouteIds) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" {} route \"r1\" when "
                      "path prefix \"/\" {} }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

// ==========================================
// 2. Route (9)
// ==========================================

TEST(TqCompilerTest, CompilesPrefixGetRoute) {
    const char* src = "policy \"pid\" { route \"r1\" when method get and path prefix \"/api\" {} }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].route_id, "r1");
    EXPECT_EQ(doc.routes[0].match_prefix, "/api");
    EXPECT_EQ(doc.routes[0].match_kind, TqRouteMatchKind::Prefix);
    EXPECT_EQ(doc.routes[0].allowed_method, TqHttpMethod::Get);
}

TEST(TqCompilerTest, CompilesExactAdminRoute) {
    const char* src =
        "policy \"pid\" { route \"r1\" when method post and path exact \"/admin\" {} }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].route_id, "r1");
    EXPECT_EQ(doc.routes[0].match_prefix, "/admin");
    EXPECT_EQ(doc.routes[0].match_kind, TqRouteMatchKind::Exact);
    EXPECT_EQ(doc.routes[0].allowed_method, TqHttpMethod::Post);
}

TEST(TqCompilerTest, DefaultsMethodToAny) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" {} }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].allowed_method, TqHttpMethod::Any);
}

TEST(TqCompilerTest, DefaultsMutationToDisabled) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" {} }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].mutation, TqMutationMode::Disabled);
}

TEST(TqCompilerTest, CompilesFailureModeFailClosed) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { failure fail_closed; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].failure_mode, TqFailureMode::FailClosed);
}

TEST(TqCompilerTest, CompilesMaxResponseBytes) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { max_response 5MiB; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].max_response_bytes, 5u * 1024u * 1024u);
}

TEST(TqCompilerTest, RejectsEmptyRouteMatcherPath) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    pol.name = "pid";
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = ""; // empty path

    TqAstStatement s1{};
    s1.kind = TqAstStatementKind::Mutate;
    s1.mutate.mode = "full";
    r.statements.push_back(s1);

    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

TEST(TqCompilerTest, RejectsDuplicateMutation) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    pol.name = "pid";
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";

    TqAstStatement s1{};
    s1.kind = TqAstStatementKind::Mutate;
    s1.mutate.mode = "full";
    r.statements.push_back(s1);

    TqAstStatement s2{};
    s2.kind = TqAstStatementKind::Mutate;
    s2.mutate.mode = "headers_only";
    r.statements.push_back(s2);

    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    opts.strict_duplicate_blocks = true;
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
}

TEST(TqCompilerTest, RejectsObserveAndMutateConflict) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { observe; mutate full; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
}

// ==========================================
// 3. Field filter (5)
// ==========================================

TEST(TqCompilerTest, CompilesAllowlistFields) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { fields allow "
                      "[\"id\", \"name\"]; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].field_filter.mode, TqFieldFilterMode::Allowlist);
    ASSERT_EQ(doc.routes[0].field_filter.fields.size(), 2u);
    EXPECT_EQ(doc.routes[0].field_filter.fields[0], "id");
    EXPECT_EQ(doc.routes[0].field_filter.fields[1], "name");
}

TEST(TqCompilerTest, CompilesDenylistFields) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { fields deny [\"secret\"]; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].field_filter.mode, TqFieldFilterMode::Denylist);
    ASSERT_EQ(doc.routes[0].field_filter.fields.size(), 1u);
    EXPECT_EQ(doc.routes[0].field_filter.fields[0], "secret");
}

TEST(TqCompilerTest, CompilesFieldsNone) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" {} }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].field_filter.mode, TqFieldFilterMode::None);
}

TEST(TqCompilerTest, RejectsDuplicateFieldsBlock) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    pol.name = "pid";
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";

    TqAstStatement s1{};
    s1.kind = TqAstStatementKind::Fields;
    s1.fields.mode = "allow";
    s1.fields.fields.push_back("a");
    r.statements.push_back(s1);

    TqAstStatement s2{};
    s2.kind = TqAstStatementKind::Fields;
    s2.fields.mode = "deny";
    s2.fields.fields.push_back("b");
    r.statements.push_back(s2);

    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
}

TEST(TqCompilerTest, RejectsDuplicateFieldNamesInStrictMode) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { fields allow [\"id\", \"id\"]; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    opts.strict_duplicate_blocks = true;
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
}

// ==========================================
// 4. Cache (10)
// ==========================================

TEST(TqCompilerTest, CompilesCacheStoreTtl) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 300s; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].cache.enabled);
    EXPECT_EQ(doc.routes[0].cache.behavior, TqCacheBehavior::Store);
    EXPECT_EQ(doc.routes[0].cache.ttl_ms, 300000u);
}

TEST(TqCompilerTest, CompilesCacheBypass) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache bypass; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].cache.enabled);
    EXPECT_EQ(doc.routes[0].cache.behavior, TqCacheBehavior::Bypass);
}

TEST(TqCompilerTest, CompilesL1Layer) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s "
                      "{ l1 enabled capacity 500 entries } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].cache.l1.enabled);
    EXPECT_EQ(doc.routes[0].cache.l1.capacity_entries, 500u);
}

TEST(TqCompilerTest, CompilesL2Layer) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s "
                      "{ l2 enabled path \"/tmp/cache\" } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].cache.l2.enabled);
    EXPECT_EQ(doc.routes[0].cache.l2.path, "/tmp/cache");
}

TEST(TqCompilerTest, CompilesPrivateCache) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s "
                      "{ private auth_scope_header \"Authorization\" } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].cache.private_cache.enabled);
    EXPECT_EQ(doc.routes[0].cache.private_cache.auth_scope_header, "Authorization");
}

TEST(TqCompilerTest, CompilesVaryHeaders) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s "
                      "{ vary by headers [\"Accept-Language\"] } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    ASSERT_EQ(doc.routes[0].cache.vary_headers.names.size(), 1u);
    EXPECT_EQ(doc.routes[0].cache.vary_headers.names[0], "Accept-Language");
}

TEST(TqCompilerTest, CompilesFieldVariantCache) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s "
                      "{ field_variant { enabled true max_variants_per_route 16 min_field_count 2 "
                      "max_field_count 12 admission_threshold 3 ttl_max 500s } } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].cache.field_variant.enabled);
    EXPECT_EQ(doc.routes[0].cache.field_variant.max_variants_per_route, 16u);
    EXPECT_EQ(doc.routes[0].cache.field_variant.min_field_count, 2u);
    EXPECT_EQ(doc.routes[0].cache.field_variant.max_field_count, 12u);
    EXPECT_EQ(doc.routes[0].cache.field_variant.admission_threshold, 3u);
    EXPECT_EQ(doc.routes[0].cache.field_variant.ttl_max_ms, 500000u);
}

TEST(TqCompilerTest, RejectsDuplicateCacheBlock) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    pol.name = "pid";
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";

    TqAstStatement s1{};
    s1.kind = TqAstStatementKind::Cache;
    s1.cache.enabled = true;
    s1.cache.behavior = "bypass";
    r.statements.push_back(s1);

    TqAstStatement s2{};
    s2.kind = TqAstStatementKind::Cache;
    s2.cache.enabled = true;
    s2.cache.behavior = "bypass";
    r.statements.push_back(s2);

    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
}

TEST(TqCompilerTest, RejectsDuplicateL1Block) {
    // Note: parser enforces duplicate checks inside cache sub-elements
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s "
                      "{ l1 enabled capacity 50 entries; l1 enabled capacity 10 entries; } } }";
    TqParseResult parse_res = parse_src(src);
    EXPECT_FALSE(parse_res.ok);
}

TEST(TqCompilerTest, RejectsDuplicateVaryHeaderInStrictMode) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s "
                      "{ vary by headers [\"Accept\", \"Accept\"] } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    opts.strict_duplicate_blocks = true;
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
}

// ==========================================
// 5. Pagination (4)
// ==========================================

TEST(TqCompilerTest, CompilesLimitOffsetPagination) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { paginate "
                      "limit_offset { limit_param \"sz\" offset_param \"off\" default_limit 50 "
                      "max_limit 200 upstream_supports_pagination true } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].pagination.enabled);
    EXPECT_EQ(doc.routes[0].pagination.mode, TqPaginationMode::LimitOffset);
    EXPECT_EQ(doc.routes[0].pagination.limit_param, "sz");
    EXPECT_EQ(doc.routes[0].pagination.offset_param, "off");
    EXPECT_EQ(doc.routes[0].pagination.default_limit, 50u);
    EXPECT_EQ(doc.routes[0].pagination.max_limit, 200u);
    EXPECT_TRUE(doc.routes[0].pagination.upstream_supports_pagination);
}

TEST(TqCompilerTest, CompilesPaginationWarningBytes) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { paginate "
                      "limit_offset { max_response_bytes_warning 5MiB } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].pagination.max_response_bytes_warning, 5u * 1024u * 1024u);
}

TEST(TqCompilerTest, CompilesCursorPaginationForValidatorToRejectLater) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { paginate cursor { "
                      "default_limit 10 } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].pagination.mode, TqPaginationMode::Cursor);
}

TEST(TqCompilerTest, RejectsDuplicatePaginationBlock) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    pol.name = "pid";
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";

    TqAstStatement s1{};
    s1.kind = TqAstStatementKind::Paginate;
    s1.paginate.enabled = true;
    s1.paginate.mode = "limit_offset";
    r.statements.push_back(s1);

    TqAstStatement s2{};
    s2.kind = TqAstStatementKind::Paginate;
    s2.paginate.enabled = true;
    s2.paginate.mode = "limit_offset";
    r.statements.push_back(s2);

    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
}

// ==========================================
// 6. Compression (5)
// ==========================================

TEST(TqCompilerTest, CompilesCompressionBlock) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { compress { min_size "
                      "1KiB eligible_content_types [\"application/json\"] } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].compression.enabled);
    EXPECT_EQ(doc.routes[0].compression.min_size_bytes, 1024u);
    ASSERT_EQ(doc.routes[0].compression.eligible_content_types.size(), 1u);
    EXPECT_EQ(doc.routes[0].compression.eligible_content_types[0], "application/json");
}

TEST(TqCompilerTest, CompilesPreferredAlgorithmOrder) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { compress { prefer "
                      "[zstd, gzip] } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    ASSERT_EQ(doc.routes[0].compression.preferred_algorithms.size(), 2u);
    EXPECT_EQ(doc.routes[0].compression.preferred_algorithms[0], TqCompressionAlgorithm::Zstd);
    EXPECT_EQ(doc.routes[0].compression.preferred_algorithms[1], TqCompressionAlgorithm::Gzip);
}

TEST(TqCompilerTest, CompilesAlreadyEncodedBehavior) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { compress { "
                      "already_encoded passthrough } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].compression.already_encoded_behavior,
              TqAlreadyEncodedBehavior::Passthrough);
}

TEST(TqCompilerTest, RejectsUnsupportedCompressionAlgorithm) {
    // Lexer parser won't allow unsupported algorithm literal inside prefer block under standard
    // parsing, butcompiler checks
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";
    TqAstStatement s{};
    s.kind = TqAstStatementKind::Compress;
    s.compress.enabled = true;
    s.compress.preferred_algorithms.push_back("invalid_algo");
    r.statements.push_back(s);
    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
}

TEST(TqCompilerTest, RejectsDuplicateCompressionAlgorithmInStrictMode) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { compress { prefer "
                      "[gzip, gzip] } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    opts.strict_duplicate_blocks = true;
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
}

// ==========================================
// 7. Coalescing (5)
// ==========================================

TEST(TqCompilerTest, CompilesCacheAssistedCoalescing) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { coalesce cache_assisted { "
        "backend_timeout 400ms handoff_buffer 150ms result_ready_retention 20ms "
        "max_waiters_per_key 32 require_cache true allow_authenticated true } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_TRUE(doc.routes[0].coalescing.enabled);
    EXPECT_EQ(doc.routes[0].coalescing.mode, TqCoalescingMode::CacheAssisted);
    EXPECT_EQ(doc.routes[0].coalescing.backend_timeout_ms, 400u);
    EXPECT_EQ(doc.routes[0].coalescing.handoff_buffer_ms, 150u);
    EXPECT_EQ(doc.routes[0].coalescing.result_ready_retention_ms, 20u);
    EXPECT_EQ(doc.routes[0].coalescing.max_waiters_per_key, 32u);
    EXPECT_TRUE(doc.routes[0].coalescing.require_cache_enabled);
    EXPECT_TRUE(doc.routes[0].coalescing.allow_authenticated);
}

TEST(TqCompilerTest, CompilesCoalescingGuardrails) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { coalesce cache_assisted { "
        "guardrails { max_follower_wait_budget 100ms max_active_follower_waiters 10 "
        "max_active_follower_waiters_per_shard 4 } } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].coalescing.max_follower_wait_budget_ms, 100u);
    EXPECT_EQ(doc.routes[0].coalescing.max_active_follower_waiters, 10u);
    EXPECT_EQ(doc.routes[0].coalescing.max_active_follower_waiters_per_shard, 4u);
}

TEST(TqCompilerTest, RejectsUnsupportedCoalescingMode) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";
    TqAstStatement s{};
    s.kind = TqAstStatementKind::Coalesce;
    s.coalesce.enabled = true;
    s.coalesce.mode = "invalid_mode";
    r.statements.push_back(s);
    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
}

TEST(TqCompilerTest, RejectsDuplicateCoalescingBlock) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    pol.name = "pid";
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";

    TqAstStatement s1{};
    s1.kind = TqAstStatementKind::Coalesce;
    s1.coalesce.enabled = true;
    s1.coalesce.mode = "cache_assisted";
    r.statements.push_back(s1);

    TqAstStatement s2{};
    s2.kind = TqAstStatementKind::Coalesce;
    s2.coalesce.enabled = true;
    s2.coalesce.mode = "cache_assisted";
    r.statements.push_back(s2);

    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
}

TEST(TqCompilerTest, RejectsDuplicateGuardrailsBlock) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { coalesce "
                      "cache_assisted { guardrails {}; guardrails {}; } } }";
    TqParseResult parse_res = parse_src(src);
    EXPECT_FALSE(parse_res.ok);
}

// ==========================================
// 8. Unit conversion (8)
// ==========================================

TEST(TqCompilerTest, CompilesMilliseconds) {
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { coalesce "
                      "cache_assisted { backend_timeout 250ms } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].coalescing.backend_timeout_ms, 250u);
}

TEST(TqCompilerTest, CompilesSecondsToMilliseconds) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl 5s; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].cache.ttl_ms, 5000u);
}

TEST(TqCompilerTest, CompilesBytes) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { max_response 100B; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].max_response_bytes, 100u);
}

TEST(TqCompilerTest, CompilesKiB) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { max_response 2KiB; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].max_response_bytes, 2048u);
}

TEST(TqCompilerTest, CompilesMiB) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { max_response 1MiB; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].max_response_bytes, 1024u * 1024u);
}

TEST(TqCompilerTest, RejectsDurationOverflow) {
    // Overflow of unit is captured during parsing/lexing phase
    const char* src = "policy \"pid\" { route \"r1\" when path prefix \"/\" { cache store ttl "
                      "9999999999999s; } }";
    TqParseResult parse_res = parse_src(src);
    EXPECT_FALSE(parse_res.ok);
}

TEST(TqCompilerTest, RejectsSizeOverflow) {
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { max_response 9999999999999MiB; } }";
    TqParseResult parse_res = parse_src(src);
    EXPECT_FALSE(parse_res.ok);
}

TEST(TqCompilerTest, RejectsUnsupportedUnit) {
    // Checked during lexer/parser
    const char* src =
        "policy \"pid\" { route \"r1\" when path prefix \"/\" { max_response 10GB; } }";
    TqParseResult parse_res = parse_src(src);
    EXPECT_FALSE(parse_res.ok);
}

// ==========================================
// 9. Integration (5)
// ==========================================

TEST(TqCompilerTest, ParseAndCompileSimpleRoute) {
    const char* src =
        "policy \"simple\" { route \"r1\" when path prefix \"/\" { mutate headers_only; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
    EXPECT_EQ(doc.routes[0].route_id, "r1");
}

TEST(TqCompilerTest, ParseAndCompileBytetaperProxyEquivalent) {
    const char* src = "policy \"proxy\" { route \"proxy-r\" when path prefix \"/\" { mutate full; "
                      "failure fail_open; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
}

TEST(TqCompilerTest, ParseAndCompileCoalescingRoute) {
    const char* src = "policy \"coalesce\" { route \"coalesce-r\" when path prefix \"/\" { "
                      "coalesce cache_assisted {}; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    ASSERT_EQ(doc.routes.size(), 1u);
}

TEST(TqCompilerTest, ParseCompileNormalizeHashIsStable) {
    const char* src =
        "policy \"coalesce\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));

    TqPolicyDocument normalized = normalize_policy_ir(doc);
    std::string hash1 = compute_policy_document_identity(normalized);
    std::string hash2 = compute_policy_document_identity(normalized);
    EXPECT_EQ(hash1, hash2);
    EXPECT_FALSE(hash1.empty());
}

TEST(TqCompilerTest, CompileThenSemanticValidateValidPolicy) {
    const char* src = "policy \"valid_doc\" { route \"r1\" when path prefix \"/\" { mutate full; "
                      "cache store ttl 60s { l1 enabled capacity 100 entries } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    ASSERT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));

    ASSERT_EQ(doc.routes.size(), 1u);
    policy::RoutePolicy runtime_policy = to_runtime_route_policy(doc.routes[0]);
    const char* reason = nullptr;
    EXPECT_TRUE(policy::validate_route_policy(runtime_policy, &reason));
}

TEST(TqCompilerTest, RejectsEmptyFieldsLists) {
    // fields allow with empty list
    {
        const char* src =
            "policy \"p1\" { route \"r1\" when path prefix \"/\" { fields allow []; } }";
        TqParseResult parse_res = parse_src(src);
        ASSERT_TRUE(parse_res.ok);
        TqPolicyDocument doc{};
        TqDiagnosticBag diags{};
        TqCompileOptions opts{};
        EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
        EXPECT_GT(diags.count, 0u);
    }
    // fields deny with empty list
    {
        const char* src =
            "policy \"p1\" { route \"r1\" when path prefix \"/\" { fields deny []; } }";
        TqParseResult parse_res = parse_src(src);
        ASSERT_TRUE(parse_res.ok);
        TqPolicyDocument doc{};
        TqDiagnosticBag diags{};
        TqCompileOptions opts{};
        EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
        EXPECT_GT(diags.count, 0u);
    }
}

TEST(TqCompilerTest, RejectsFieldsNoneWithFields) {
    TqAstDocument ast{};
    TqAstPolicyDecl pol{};
    pol.name = "pid";
    TqAstRouteDecl r{};
    r.name = "r1";
    r.has_when = true;
    r.when_clause.match_expr.path = "/";

    TqAstStatement s1{};
    s1.kind = TqAstStatementKind::Fields;
    s1.fields.mode = "none";
    s1.fields.fields.push_back("id"); // non-empty field specified for none
    r.statements.push_back(s1);

    pol.routes.push_back(r);
    ast.policies.push_back(pol);

    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(ast, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

TEST(TqCompilerTest, RejectsCacheStoreWithoutTTL) {
    const char* src = "policy \"p1\" { route \"r1\" when path prefix \"/\" { cache store; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);
    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

TEST(TqCompilerTest, RejectsEmptyVaryHeaders) {
    const char* src = "policy \"p1\" { route \"r1\" when path prefix \"/\" { cache store ttl 60s { "
                      "vary by headers [] } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);
    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

TEST(TqCompilerTest, RejectsEmptyCompressionPrefer) {
    const char* src =
        "policy \"p1\" { route \"r1\" when path prefix \"/\" { compress { prefer [] } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);
    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_FALSE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_GT(diags.count, 0u);
}

TEST(TqCompilerTest, CompilesCacheStoreTtl0s) {
    const char* src =
        "policy \"p1\" { route \"r1\" when path prefix \"/\" { cache store ttl 0s; } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);
    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_EQ(diags.count, 0u);
}

TEST(TqCompilerTest, CompilesFieldVariantCacheTtl0s) {
    const char* src = "policy \"p1\" { route \"r1\" when path prefix \"/\" { cache store { "
                      "field_variant { enabled true ttl_max 0s } } } }";
    TqParseResult parse_res = parse_src(src);
    ASSERT_TRUE(parse_res.ok);
    TqPolicyDocument doc{};
    TqDiagnosticBag diags{};
    TqCompileOptions opts{};
    EXPECT_TRUE(compile_taperquery_ast_to_policy_ir(parse_res.document, opts, &doc, &diags));
    EXPECT_EQ(diags.count, 0u);
}

} // namespace bytetaper::taperquery

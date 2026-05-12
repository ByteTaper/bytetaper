// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "taperquery/tq_source_location.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

struct TqAstStringLiteral {
    std::string value{};
    TqSourceSpan span{};
};

struct TqAstIntegerLiteral {
    std::uint32_t value = 0;
    TqSourceSpan span{};
};

struct TqAstDurationLiteral {
    std::uint32_t value_ms = 0;
    TqSourceSpan span{};
};

struct TqAstSizeLiteral {
    std::uint32_t value_bytes = 0;
    TqSourceSpan span{};
};

struct TqAstMatchExpr {
    std::string method{};
    std::string path_kind{}; // "prefix" or "exact"
    std::string path{};
    TqSourceSpan span{};
};

struct TqAstWhenClause {
    TqAstMatchExpr match_expr{};
    TqSourceSpan span{};
};

struct TqAstMutateStmt {
    std::string mode{}; // "headers_only", "full", "none"
    TqSourceSpan span{};
};

struct TqAstFailureStmt {
    std::string mode{}; // "fail_open", "fail_closed"
    TqSourceSpan span{};
};

struct TqAstMaxResponseStmt {
    std::uint32_t limit_bytes = 0;
    TqSourceSpan span{};
};

struct TqAstCacheL1Clause {
    bool enabled = false;
    std::uint32_t capacity_entries = 0;
    TqSourceSpan span{};
};

struct TqAstCacheL2Clause {
    bool enabled = false;
    std::string path{};
    TqSourceSpan span{};
};

struct TqAstPrivateCacheClause {
    bool enabled = false;
    std::string auth_scope_header{};
    TqSourceSpan span{};
};

struct TqAstVaryHeadersClause {
    std::vector<std::string> headers{};
    TqSourceSpan span{};
};

struct TqAstFieldVariantClause {
    bool enabled = false;
    std::uint32_t max_variants_per_route = 0;
    std::uint32_t min_field_count = 0;
    std::uint32_t max_field_count = 0;
    std::uint32_t admission_threshold = 0;
    std::uint32_t ttl_max_ms = 0;
    TqSourceSpan span{};
};

struct TqAstCacheStmt {
    bool enabled = false;
    std::string behavior{}; // "store", "bypass"
    std::uint32_t ttl_ms = 0;
    TqAstCacheL1Clause l1{};
    TqAstCacheL2Clause l2{};
    TqAstPrivateCacheClause private_cache{};
    TqAstVaryHeadersClause vary_headers{};
    TqAstFieldVariantClause field_variant{};
    TqSourceSpan span{};
};

struct TqAstFieldsStmt {
    std::string mode{}; // "allow", "deny"
    std::vector<std::string> fields{};
    TqSourceSpan span{};
};

struct TqAstPaginateStmt {
    bool enabled = false;
    std::string mode{}; // "limit_offset", "cursor"
    std::string limit_param{};
    std::string offset_param{};
    std::uint32_t default_limit = 0;
    std::uint32_t max_limit = 0;
    bool upstream_supports_pagination = false;
    std::uint32_t max_response_bytes_warning = 0;
    TqSourceSpan span{};
};

struct TqAstCompressStmt {
    bool enabled = false;
    std::uint32_t min_size_bytes = 0;
    std::vector<std::string> eligible_content_types{};
    std::vector<std::string> preferred_algorithms{}; // "gzip", "brotli", "zstd"
    std::string already_encoded_behavior{};          // "skip", "passthrough"
    TqSourceSpan span{};
};

struct TqAstGuardrailsClause {
    bool enabled = false;
    std::uint32_t max_follower_wait_budget_ms = 0;
    std::uint32_t max_active_follower_waiters = 0;
    std::uint32_t max_active_follower_waiters_per_shard = 0;
    TqSourceSpan span{};
};

struct TqAstCoalesceStmt {
    bool enabled = false;
    std::string mode{}; // "cache_assisted"
    std::uint32_t backend_timeout_ms = 0;
    std::uint32_t handoff_buffer_ms = 0;
    std::uint32_t result_ready_retention_ms = 0;
    std::uint32_t max_waiters_per_key = 0;
    bool require_cache_enabled = false;
    bool allow_authenticated = false;
    TqAstGuardrailsClause guardrails{};
    TqSourceSpan span{};
};

enum class TqAstStatementKind : std::uint8_t {
    Mutate,
    Failure,
    MaxResponse,
    Cache,
    Fields,
    Paginate,
    Compress,
    Coalesce,
};

struct TqAstStatement {
    TqAstStatementKind kind = TqAstStatementKind::Mutate;
    TqSourceSpan span{};

    TqAstMutateStmt mutate{};
    TqAstFailureStmt failure{};
    TqAstMaxResponseStmt max_response{};
    TqAstCacheStmt cache{};
    TqAstFieldsStmt fields{};
    TqAstPaginateStmt paginate{};
    TqAstCompressStmt compress{};
    TqAstCoalesceStmt coalesce{};
};

struct TqAstRouteDecl {
    std::string name{};
    bool has_when = false;
    TqAstWhenClause when_clause{};
    std::vector<TqAstStatement> statements{};
    TqSourceSpan span{};
};

struct TqAstPolicyDecl {
    std::string name{};
    std::string schema{};
    std::string sha256{};
    std::vector<TqAstRouteDecl> routes{};
    TqSourceSpan span{};
};

struct TqAstDocument {
    std::vector<TqAstPolicyDecl> policies{};
    std::vector<TqAstRouteDecl> top_level_routes{};
    TqSourceSpan span{};
};

} // namespace bytetaper::taperquery

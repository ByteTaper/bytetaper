// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_compiler.h"

#include "taperquery/policy_ir_version.h"

#include <algorithm>
#include <set>
#include <string>

namespace bytetaper::taperquery {

namespace {

struct RouteCompileSeen {
    bool matcher = false;
    bool mutate = false;
    bool failure = false;
    bool max_response = false;
    bool fields = false;
    bool cache = false;
    bool paginate = false;
    bool compress = false;
    bool coalesce = false;
    bool observe = false;
};

bool compile_mutation(const TqAstMutateStmt& ast, TqRoutePolicy* out_rp,
                      TqDiagnosticBag* diagnostics) {
    if (ast.mode == "full") {
        out_rp->mutation = TqMutationMode::Full;
    } else if (ast.mode == "headers_only") {
        out_rp->mutation = TqMutationMode::HeadersOnly;
    } else if (ast.mode == "none" || ast.mode == "disabled") {
        out_rp->mutation = TqMutationMode::Disabled;
    } else {
        report_tq_diagnostic(
            diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken, ast.span,
            "[TQ-COMP-006] unsupported enum value for mutate mode: '%s'", ast.mode.c_str());
        return false;
    }
    return true;
}

bool compile_failure_mode(const TqAstFailureStmt& ast, TqRoutePolicy* out_rp,
                          TqDiagnosticBag* diagnostics) {
    if (ast.mode == "fail_open") {
        out_rp->failure_mode = TqFailureMode::FailOpen;
    } else if (ast.mode == "fail_closed") {
        out_rp->failure_mode = TqFailureMode::FailClosed;
    } else {
        report_tq_diagnostic(
            diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken, ast.span,
            "[TQ-COMP-006] unsupported enum value for failure mode: '%s'", ast.mode.c_str());
        return false;
    }
    return true;
}

bool compile_max_response(const TqAstMaxResponseStmt& ast, TqRoutePolicy* out_rp,
                          TqDiagnosticBag*) {
    out_rp->max_response_bytes = ast.limit_bytes;
    return true;
}

bool compile_field_filter(const TqAstFieldsStmt& ast, TqRoutePolicy* out_rp,
                          const TqCompileOptions& options, TqDiagnosticBag* diagnostics) {
    if (ast.mode == "allow") {
        out_rp->field_filter.mode = TqFieldFilterMode::Allowlist;
        if (ast.fields.empty()) {
            report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnexpectedToken, ast.span,
                                 "[TQ-COMP-015] fields allowlist cannot be empty");
            return false;
        }
    } else if (ast.mode == "deny") {
        out_rp->field_filter.mode = TqFieldFilterMode::Denylist;
        if (ast.fields.empty()) {
            report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnexpectedToken, ast.span,
                                 "[TQ-COMP-015] fields denylist cannot be empty");
            return false;
        }
    } else if (ast.mode.empty() || ast.mode == "none") {
        out_rp->field_filter.mode = TqFieldFilterMode::None;
        if (!ast.fields.empty()) {
            report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnexpectedToken, ast.span,
                                 "[TQ-COMP-016] fields none cannot specify fields");
            return false;
        }
    } else {
        report_tq_diagnostic(
            diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken, ast.span,
            "[TQ-COMP-006] unsupported enum value for field filter mode: '%s'", ast.mode.c_str());
        return false;
    }

    out_rp->field_filter.fields = ast.fields;

    if (options.strict_duplicate_blocks) {
        std::set<std::string> unique_fields;
        for (const auto& f : ast.fields) {
            if (!unique_fields.insert(f).second) {
                report_tq_diagnostic(
                    diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken,
                    ast.span, "[TQ-COMP-011] duplicate list entry in fields: '%s'", f.c_str());
                return false;
            }
        }
    }
    return true;
}

bool compile_cache(const TqAstCacheStmt& ast, TqRoutePolicy* out_rp,
                   const TqCompileOptions& options, TqDiagnosticBag* diagnostics) {
    out_rp->cache.enabled = ast.enabled;

    if (ast.behavior == "store") {
        out_rp->cache.behavior = TqCacheBehavior::Store;
    } else if (ast.behavior == "bypass") {
        out_rp->cache.behavior = TqCacheBehavior::Bypass;
    } else if (ast.behavior.empty() || ast.behavior == "default") {
        out_rp->cache.behavior = TqCacheBehavior::Default;
    } else {
        report_tq_diagnostic(
            diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken, ast.span,
            "[TQ-COMP-006] unsupported enum value for cache behavior: '%s'", ast.behavior.c_str());
        return false;
    }

    out_rp->cache.ttl_ms = ast.ttl_ms;

    // L1 layer
    out_rp->cache.l1.enabled = ast.l1.enabled;
    out_rp->cache.l1.capacity_entries = ast.l1.capacity_entries;

    // L2 layer
    out_rp->cache.l2.enabled = ast.l2.enabled;
    out_rp->cache.l2.path = ast.l2.path;

    // Private cache
    out_rp->cache.private_cache.enabled = ast.private_cache.enabled;
    out_rp->cache.private_cache.auth_scope_header = ast.private_cache.auth_scope_header;

    // Vary headers
    out_rp->cache.vary_headers.names = ast.vary_headers.headers;
    if (ast.vary_specified && ast.vary_headers.headers.empty()) {
        report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedToken, ast.vary_headers.span,
                             "[TQ-COMP-018] vary by headers list cannot be empty");
        return false;
    }
    if (options.strict_duplicate_blocks) {
        std::set<std::string> unique_headers;
        for (const auto& h : ast.vary_headers.headers) {
            if (!unique_headers.insert(h).second) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, ast.vary_headers.span,
                                     "[TQ-COMP-011] duplicate list entry in vary headers: '%s'",
                                     h.c_str());
                return false;
            }
        }
    }

    // Field variant cache
    out_rp->cache.field_variant.enabled = ast.field_variant.enabled;
    out_rp->cache.field_variant.max_variants_per_route = ast.field_variant.max_variants_per_route;
    out_rp->cache.field_variant.min_field_count = ast.field_variant.min_field_count;
    out_rp->cache.field_variant.max_field_count = ast.field_variant.max_field_count;
    out_rp->cache.field_variant.admission_threshold = ast.field_variant.admission_threshold;
    out_rp->cache.field_variant.ttl_max_ms = ast.field_variant.ttl_max_ms;

    if (out_rp->cache.behavior == TqCacheBehavior::Store) {
        bool has_ttl = ast.ttl_specified;
        if (ast.field_variant.enabled && ast.field_variant.ttl_max_specified) {
            has_ttl = true;
        }
        if (!has_ttl) {
            report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnexpectedToken, ast.span,
                                 "[TQ-COMP-017] cache store behavior requires a TTL");
            return false;
        }
    }

    return true;
}

bool compile_pagination(const TqAstPaginateStmt& ast, TqRoutePolicy* out_rp,
                        TqDiagnosticBag* diagnostics) {
    out_rp->pagination.enabled = ast.enabled;

    if (ast.mode == "limit_offset") {
        out_rp->pagination.mode = TqPaginationMode::LimitOffset;
    } else if (ast.mode == "cursor") {
        out_rp->pagination.mode = TqPaginationMode::Cursor;
    } else if (ast.mode.empty() || ast.mode == "none") {
        out_rp->pagination.mode = TqPaginationMode::None;
    } else {
        report_tq_diagnostic(
            diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken, ast.span,
            "[TQ-COMP-006] unsupported enum value for pagination mode: '%s'", ast.mode.c_str());
        return false;
    }

    if (!ast.limit_param.empty()) {
        out_rp->pagination.limit_param = ast.limit_param;
    }
    if (!ast.offset_param.empty()) {
        out_rp->pagination.offset_param = ast.offset_param;
    }

    out_rp->pagination.default_limit = ast.default_limit;
    out_rp->pagination.max_limit = ast.max_limit;
    out_rp->pagination.upstream_supports_pagination = ast.upstream_supports_pagination;
    out_rp->pagination.max_response_bytes_warning = ast.max_response_bytes_warning;

    return true;
}

bool compile_compression(const TqAstCompressStmt& ast, TqRoutePolicy* out_rp,
                         const TqCompileOptions& options, TqDiagnosticBag* diagnostics) {
    out_rp->compression.enabled = ast.enabled;
    out_rp->compression.min_size_bytes = ast.min_size_bytes;
    out_rp->compression.eligible_content_types = ast.eligible_content_types;

    if (ast.already_encoded_behavior == "skip") {
        out_rp->compression.already_encoded_behavior = TqAlreadyEncodedBehavior::Skip;
    } else if (ast.already_encoded_behavior == "passthrough") {
        out_rp->compression.already_encoded_behavior = TqAlreadyEncodedBehavior::Passthrough;
    } else if (ast.already_encoded_behavior.empty()) {
        out_rp->compression.already_encoded_behavior = TqAlreadyEncodedBehavior::Skip;
    } else {
        report_tq_diagnostic(
            diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken, ast.span,
            "[TQ-COMP-006] unsupported enum value for already encoded behavior: '%s'",
            ast.already_encoded_behavior.c_str());
        return false;
    }

    if (ast.prefer_specified && ast.preferred_algorithms.empty()) {
        report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedToken, ast.span,
                             "[TQ-COMP-019] compression prefer list cannot be empty");
        return false;
    }

    out_rp->compression.preferred_algorithms.clear();
    std::set<TqCompressionAlgorithm> seen_algos;
    for (const auto& algo : ast.preferred_algorithms) {
        TqCompressionAlgorithm ca = TqCompressionAlgorithm::None;
        if (algo == "gzip") {
            ca = TqCompressionAlgorithm::Gzip;
        } else if (algo == "brotli") {
            ca = TqCompressionAlgorithm::Brotli;
        } else if (algo == "zstd") {
            ca = TqCompressionAlgorithm::Zstd;
        } else {
            report_tq_diagnostic(
                diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken,
                ast.span, "[TQ-COMP-013] unsupported compression algorithm: '%s'", algo.c_str());
            return false;
        }

        if (options.strict_duplicate_blocks) {
            if (!seen_algos.insert(ca).second) {
                report_tq_diagnostic(
                    diagnostics, TqDiagnosticSeverity::Error, TqDiagnosticCode::UnexpectedToken,
                    ast.span, "[TQ-COMP-011] duplicate list entry in preferred algorithms: '%s'",
                    algo.c_str());
                return false;
            }
        }
        out_rp->compression.preferred_algorithms.push_back(ca);
    }

    return true;
}

bool compile_coalescing(const TqAstCoalesceStmt& ast, TqRoutePolicy* out_rp,
                        TqDiagnosticBag* diagnostics) {
    out_rp->coalescing.enabled = ast.enabled;

    if (ast.mode == "cache_assisted") {
        out_rp->coalescing.mode = TqCoalescingMode::CacheAssisted;
    } else if (ast.mode.empty()) {
        out_rp->coalescing.mode = TqCoalescingMode::CacheAssisted;
    } else {
        report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedToken, ast.span,
                             "[TQ-COMP-012] unsupported coalescing mode: '%s'", ast.mode.c_str());
        return false;
    }

    out_rp->coalescing.backend_timeout_ms = ast.backend_timeout_ms;
    out_rp->coalescing.handoff_buffer_ms = ast.handoff_buffer_ms;
    out_rp->coalescing.result_ready_retention_ms = ast.result_ready_retention_ms;
    out_rp->coalescing.max_waiters_per_key = ast.max_waiters_per_key;
    out_rp->coalescing.require_cache_enabled = ast.require_cache_enabled;
    out_rp->coalescing.allow_authenticated = ast.allow_authenticated;

    out_rp->coalescing.max_follower_wait_budget_ms = ast.guardrails.max_follower_wait_budget_ms;
    out_rp->coalescing.max_active_follower_waiters = ast.guardrails.max_active_follower_waiters;
    out_rp->coalescing.max_active_follower_waiters_per_shard =
        ast.guardrails.max_active_follower_waiters_per_shard;

    return true;
}

bool compile_route(const TqAstRouteDecl& ast, const TqCompileOptions& options,
                   TqRoutePolicy* out_rp, TqDiagnosticBag* diagnostics) {
    out_rp->route_id = ast.name;

    if (ast.has_when) {
        if (ast.when_clause.match_expr.path.empty()) {
            report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnexpectedToken, ast.when_clause.match_expr.span,
                                 "[TQ-COMP-014] route matcher path cannot be empty");
            return false;
        }
        out_rp->match_prefix = ast.when_clause.match_expr.path;
        if (ast.when_clause.match_expr.path_kind == "exact") {
            out_rp->match_kind = TqRouteMatchKind::Exact;
        } else {
            out_rp->match_kind = TqRouteMatchKind::Prefix;
        }

        const auto& method = ast.when_clause.match_expr.method;
        if (method == "get" || method == "GET") {
            out_rp->allowed_method = TqHttpMethod::Get;
        } else if (method == "post" || method == "POST") {
            out_rp->allowed_method = TqHttpMethod::Post;
        } else if (method == "put" || method == "PUT") {
            out_rp->allowed_method = TqHttpMethod::Put;
        } else if (method == "delete" || method == "DELETE") {
            out_rp->allowed_method = TqHttpMethod::Delete;
        } else if (method == "patch" || method == "PATCH") {
            out_rp->allowed_method = TqHttpMethod::Patch;
        } else if (method.empty() || method == "any" || method == "ANY") {
            out_rp->allowed_method = TqHttpMethod::Any;
        } else {
            report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnexpectedToken, ast.when_clause.match_expr.span,
                                 "[TQ-COMP-006] unsupported enum value for route method: '%s'",
                                 method.c_str());
            return false;
        }
    } else {
        if (!options.allow_partial_document) {
            report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnexpectedToken, ast.span,
                                 "[TQ-COMP-005] missing route matcher");
            return false;
        }
    }

    RouteCompileSeen seen{};

    for (const auto& stmt : ast.statements) {
        switch (stmt.kind) {
        case TqAstStatementKind::Mutate: {
            if (options.strict_duplicate_blocks && seen.mutate) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'mutate'");
                return false;
            }
            seen.mutate = true;
            if (!compile_mutation(stmt.mutate, out_rp, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::Failure: {
            if (options.strict_duplicate_blocks && seen.failure) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'failure'");
                return false;
            }
            seen.failure = true;
            if (!compile_failure_mode(stmt.failure, out_rp, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::MaxResponse: {
            if (options.strict_duplicate_blocks && seen.max_response) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'max_response'");
                return false;
            }
            seen.max_response = true;
            if (!compile_max_response(stmt.max_response, out_rp, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::Fields: {
            if (options.strict_duplicate_blocks && seen.fields) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'fields'");
                return false;
            }
            seen.fields = true;
            if (!compile_field_filter(stmt.fields, out_rp, options, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::Cache: {
            if (options.strict_duplicate_blocks && seen.cache) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'cache'");
                return false;
            }
            seen.cache = true;
            if (!compile_cache(stmt.cache, out_rp, options, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::Paginate: {
            if (options.strict_duplicate_blocks && seen.paginate) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'paginate'");
                return false;
            }
            seen.paginate = true;
            if (!compile_pagination(stmt.paginate, out_rp, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::Compress: {
            if (options.strict_duplicate_blocks && seen.compress) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'compress'");
                return false;
            }
            seen.compress = true;
            if (!compile_compression(stmt.compress, out_rp, options, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::Coalesce: {
            if (options.strict_duplicate_blocks && seen.coalesce) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'coalesce'");
                return false;
            }
            seen.coalesce = true;
            if (!compile_coalescing(stmt.coalesce, out_rp, diagnostics)) {
                return false;
            }
            break;
        }
        case TqAstStatementKind::Observe: {
            if (options.strict_duplicate_blocks && seen.observe) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, stmt.span,
                                     "[TQ-COMP-002] duplicate route block: 'observe'");
                return false;
            }
            seen.observe = true;
            out_rp->mutation = TqMutationMode::Disabled;
            break;
        }
        }
    }

    if (seen.mutate && seen.observe) {
        report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedToken, ast.span,
                             "[TQ-COMP-009] conflicting declarations (observe + mutate)");
        return false;
    }

    return true;
}

} // namespace

bool compile_taperquery_ast_to_policy_ir(const TqAstDocument& ast, const TqCompileOptions& options,
                                         TqPolicyDocument* out, TqDiagnosticBag* diagnostics) {

    if (ast.policies.empty() && !options.allow_partial_document) {
        report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedToken, ast.span,
                             "[TQ-COMP-004] missing policy declaration");
        return false;
    }

    if (ast.policies.size() > 1) {
        report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedToken, ast.span,
                             "[TQ-COMP-010] duplicate policy metadata declaration");
        return false;
    }

    out->routes.clear();

    if (!ast.policies.empty()) {
        const auto& pol = ast.policies[0];
        out->document_id = pol.name;
        if (pol.schema.empty()) {
            out->version.source_schema_version = "tq/v1";
        } else {
            out->version.source_schema_version = pol.schema;
        }
        out->schema_version = out->version.source_schema_version;
        out->version.policy_ir_version = kCurrentPolicyIrVersion;
        out->version.identity_version = kCurrentPolicyIdentityVersion;
        out->expected_base_sha = pol.sha256;

        std::set<std::string> seen_route_ids;
        for (const auto& ast_route : pol.routes) {
            if (!seen_route_ids.insert(ast_route.name).second) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, ast_route.span,
                                     "[TQ-COMP-001] duplicate route id: '%s'",
                                     ast_route.name.c_str());
                return false;
            }

            TqRoutePolicy rp{};
            if (!compile_route(ast_route, options, &rp, diagnostics)) {
                return false;
            }
            out->routes.push_back(rp);
        }
    } else {
        // partial document or top-level routes
        out->version.source_schema_version = "tq/v1";
        out->schema_version = "tq/v1";
        out->version.policy_ir_version = kCurrentPolicyIrVersion;
        out->version.identity_version = kCurrentPolicyIdentityVersion;
        std::set<std::string> seen_route_ids;
        for (const auto& ast_route : ast.top_level_routes) {
            if (!seen_route_ids.insert(ast_route.name).second) {
                report_tq_diagnostic(diagnostics, TqDiagnosticSeverity::Error,
                                     TqDiagnosticCode::UnexpectedToken, ast_route.span,
                                     "[TQ-COMP-001] duplicate route id: '%s'",
                                     ast_route.name.c_str());
                return false;
            }

            TqRoutePolicy rp{};
            if (!compile_route(ast_route, options, &rp, diagnostics)) {
                return false;
            }
            out->routes.push_back(rp);
        }
    }

    return true;
}

} // namespace bytetaper::taperquery

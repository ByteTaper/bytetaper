// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_printer.h"

namespace bytetaper::taperquery {

std::string print_canonical_policy_ir(const TqPolicyDocument& policy) {
    std::string out;
    if (!policy.version.source_schema_version.empty()) {
        out += "source_schema_version: " + policy.version.source_schema_version + "\n";
    }
    if (!policy.version.policy_ir_version.empty()) {
        out += "policy_ir_version: " + policy.version.policy_ir_version + "\n";
    }
    if (!policy.version.identity_version.empty()) {
        out += "identity_version: " + policy.version.identity_version + "\n";
    }
    if (!policy.document_id.empty()) {
        out += "document_id: " + policy.document_id + "\n";
    }
    if (!policy.source_name.empty()) {
        out += "source_name: " + policy.source_name + "\n";
    }
    if (!policy.expected_base_sha.empty()) {
        out += "expected_base_sha: " + policy.expected_base_sha + "\n";
    }

    for (const auto& route : policy.routes) {
        out += "route:\n";
        out += "  route_id: " + route.route_id + "\n";
        out += "  match_prefix: " + route.match_prefix + "\n";
        out += "  match_kind: " +
               std::string(route.match_kind == TqRouteMatchKind::Prefix ? "prefix" : "exact") +
               "\n";
        if (route.mutation != TqMutationMode::Disabled) {
            out += "  mutation: " +
                   std::string(route.mutation == TqMutationMode::HeadersOnly ? "headers_only"
                                                                             : "full") +
                   "\n";
        }
        if (route.allowed_method != TqHttpMethod::Any) {
            std::string method = "any";
            switch (route.allowed_method) {
            case TqHttpMethod::Get:
                method = "get";
                break;
            case TqHttpMethod::Post:
                method = "post";
                break;
            case TqHttpMethod::Put:
                method = "put";
                break;
            case TqHttpMethod::Delete:
                method = "delete";
                break;
            case TqHttpMethod::Patch:
                method = "patch";
                break;
            default:
                break;
            }
            out += "  allowed_method: " + method + "\n";
        }
        if (route.max_response_bytes > 0) {
            out += "  max_response_bytes: " + std::to_string(route.max_response_bytes) + "\n";
        }
        if (route.failure_mode != TqFailureMode::FailOpen) {
            out += "  failure_mode: " +
                   std::string(route.failure_mode == TqFailureMode::FailClosed ? "fail_closed"
                                                                               : "fail_open") +
                   "\n";
        }

        // Cache policy
        if (route.cache.enabled || route.cache.behavior != TqCacheBehavior::Default) {
            out += "  cache:\n";
            out += "    enabled: " + std::string(route.cache.enabled ? "true" : "false") + "\n";
            if (route.cache.behavior != TqCacheBehavior::Default) {
                out += "    behavior: " +
                       std::string(route.cache.behavior == TqCacheBehavior::Bypass ? "bypass"
                                                                                   : "store") +
                       "\n";
            }
            // Include ttl_ms if cache is enabled (even if 0, to expose design mismatch or
            // configuration errors)
            if (route.cache.enabled || route.cache.ttl_ms > 0) {
                out += "    ttl_ms: " + std::to_string(route.cache.ttl_ms) + "\n";
            }
            if (route.cache.l1.enabled || route.cache.l1.capacity_entries > 0) {
                out += "    l1:\n";
                out += "      enabled: " + std::string(route.cache.l1.enabled ? "true" : "false") +
                       "\n";
                out +=
                    "      capacity_entries: " + std::to_string(route.cache.l1.capacity_entries) +
                    "\n";
            }
            if (route.cache.l2.enabled || !route.cache.l2.path.empty()) {
                out += "    l2:\n";
                out += "      enabled: " + std::string(route.cache.l2.enabled ? "true" : "false") +
                       "\n";
                out += "      path: " + route.cache.l2.path + "\n";
            }
            if (route.cache.private_cache.enabled ||
                !route.cache.private_cache.auth_scope_header.empty()) {
                out += "    private_cache:\n";
                out += "      enabled: " +
                       std::string(route.cache.private_cache.enabled ? "true" : "false") + "\n";
                out += "      auth_scope_header:";
                if (!route.cache.private_cache.auth_scope_header.empty()) {
                    out += " " + route.cache.private_cache.auth_scope_header;
                }
                out += "\n";
            }
            if (route.cache.field_variant.enabled) {
                out += "    field_variant:\n";
                out += "      enabled: true\n";
                out += "      max_variants_per_route: " +
                       std::to_string(route.cache.field_variant.max_variants_per_route) + "\n";
                out += "      min_field_count: " +
                       std::to_string(route.cache.field_variant.min_field_count) + "\n";
                out += "      max_field_count: " +
                       std::to_string(route.cache.field_variant.max_field_count) + "\n";
                out += "      admission_threshold: " +
                       std::to_string(route.cache.field_variant.admission_threshold) + "\n";
                out += "      ttl_max_ms: " + std::to_string(route.cache.field_variant.ttl_max_ms) +
                       "\n";
            }
            if (!route.cache.vary_headers.names.empty()) {
                out += "    vary_headers:\n";
                for (const auto& name : route.cache.vary_headers.names) {
                    out += "      - " + name + "\n";
                }
            }
            if (route.cache.invalidation.enabled) {
                out += "    invalidation:\n";
                out += "      enabled: true\n";
                if (!route.cache.invalidation.on_methods.empty()) {
                    out += "      on_methods:\n";
                    for (const auto& m : route.cache.invalidation.on_methods) {
                        out += "        - " + m + "\n";
                    }
                }
                out += "      timing: " + route.cache.invalidation.timing + "\n";
                out += "      success_status:\n";
                out +=
                    "        min: " + std::to_string(route.cache.invalidation.success_status_min) +
                    "\n";
                out +=
                    "        max: " + std::to_string(route.cache.invalidation.success_status_max) +
                    "\n";
                if (!route.cache.invalidation.targets.empty()) {
                    out += "      targets:\n";
                    for (const auto& t : route.cache.invalidation.targets) {
                        out += "        - route_id: " + t.route_id + "\n";
                        std::string strategy_str = "route_epoch";
                        if (t.strategy == TqCacheInvalidationStrategy::ExactKey)
                            strategy_str = "exact_key";
                        else if (t.strategy == TqCacheInvalidationStrategy::Prefix)
                            strategy_str = "prefix";
                        out += "          strategy: " + strategy_str + "\n";
                    }
                }
            }
        }

        // Field Filter
        if (route.field_filter.mode != TqFieldFilterMode::None) {
            out += "  field_filter:\n";
            out +=
                "    mode: " +
                std::string(route.field_filter.mode == TqFieldFilterMode::Allowlist ? "allowlist"
                                                                                    : "denylist") +
                "\n";
            out += "    fields:\n";
            for (const auto& f : route.field_filter.fields) {
                out += "      - " + f + "\n";
            }
        }

        // Pagination
        if (route.pagination.enabled) {
            out += "  pagination:\n";
            out += "    enabled: true\n";
            std::string pag_mode = "none";
            if (route.pagination.mode == TqPaginationMode::LimitOffset) {
                pag_mode = "limit_offset";
            } else if (route.pagination.mode == TqPaginationMode::Cursor) {
                pag_mode = "cursor";
            }
            out += "    mode: " + pag_mode + "\n";
            out += "    limit_param: " + route.pagination.limit_param + "\n";
            out += "    offset_param: " + route.pagination.offset_param + "\n";
            out += "    default_limit: " + std::to_string(route.pagination.default_limit) + "\n";
            out += "    max_limit: " + std::to_string(route.pagination.max_limit) + "\n";
            out += "    upstream_supports_pagination: " +
                   std::string(route.pagination.upstream_supports_pagination ? "true" : "false") +
                   "\n";
            if (route.pagination.max_response_bytes_warning > 0) {
                out += "    max_response_bytes_warning: " +
                       std::to_string(route.pagination.max_response_bytes_warning) + "\n";
            }
        }

        // Compression
        if (route.compression.enabled) {
            out += "  compression:\n";
            out += "    enabled: true\n";
            out += "    min_size_bytes: " + std::to_string(route.compression.min_size_bytes) + "\n";
            if (!route.compression.eligible_content_types.empty()) {
                out += "    eligible_content_types:\n";
                for (const auto& ct : route.compression.eligible_content_types) {
                    out += "      - " + ct + "\n";
                }
            }
            if (!route.compression.preferred_algorithms.empty()) {
                out += "    preferred_algorithms:\n";
                for (const auto& algo : route.compression.preferred_algorithms) {
                    std::string a_str = "none";
                    if (algo == TqCompressionAlgorithm::Gzip) {
                        a_str = "gzip";
                    } else if (algo == TqCompressionAlgorithm::Brotli) {
                        a_str = "brotli";
                    } else if (algo == TqCompressionAlgorithm::Zstd) {
                        a_str = "zstd";
                    }
                    out += "      - " + a_str + "\n";
                }
            }
            if (route.compression.already_encoded_behavior != TqAlreadyEncodedBehavior::Skip) {
                out += "    already_encoded_behavior: passthrough\n";
            }
        }

        // Coalescing
        if (route.coalescing.enabled) {
            out += "  coalescing:\n";
            out += "    enabled: true\n";
            out += "    mode: cache_assisted\n";
            out +=
                "    backend_timeout_ms: " + std::to_string(route.coalescing.backend_timeout_ms) +
                "\n";
            out += "    handoff_buffer_ms: " + std::to_string(route.coalescing.handoff_buffer_ms) +
                   "\n";
            out += "    result_ready_retention_ms: " +
                   std::to_string(route.coalescing.result_ready_retention_ms) + "\n";
            out +=
                "    max_waiters_per_key: " + std::to_string(route.coalescing.max_waiters_per_key) +
                "\n";
            out += "    require_cache_enabled: " +
                   std::string(route.coalescing.require_cache_enabled ? "true" : "false") + "\n";
            out += "    allow_authenticated: " +
                   std::string(route.coalescing.allow_authenticated ? "true" : "false") + "\n";
            if (route.coalescing.max_follower_wait_budget_ms > 0) {
                out += "    max_follower_wait_budget_ms: " +
                       std::to_string(route.coalescing.max_follower_wait_budget_ms) + "\n";
            }
            if (route.coalescing.max_active_follower_waiters > 0) {
                out += "    max_active_follower_waiters: " +
                       std::to_string(route.coalescing.max_active_follower_waiters) + "\n";
            }
            if (route.coalescing.max_active_follower_waiters_per_shard > 0) {
                out += "    max_active_follower_waiters_per_shard: " +
                       std::to_string(route.coalescing.max_active_follower_waiters_per_shard) +
                       "\n";
            }
        }
    }
    return out;
}

} // namespace bytetaper::taperquery

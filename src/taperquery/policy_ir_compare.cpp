// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_compare.h"

#include <algorithm>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

namespace {

std::string bool_to_string(bool b) {
    return b ? "true" : "false";
}

std::string match_kind_to_string(TqRouteMatchKind kind) {
    return (kind == TqRouteMatchKind::Exact) ? "exact" : "prefix";
}

std::string mutation_mode_to_string(TqMutationMode mode) {
    switch (mode) {
    case TqMutationMode::HeadersOnly:
        return "headers_only";
    case TqMutationMode::Full:
        return "full";
    default:
        return "disabled";
    }
}

std::string http_method_to_string(TqHttpMethod method) {
    switch (method) {
    case TqHttpMethod::Get:
        return "get";
    case TqHttpMethod::Post:
        return "post";
    case TqHttpMethod::Put:
        return "put";
    case TqHttpMethod::Delete:
        return "delete";
    case TqHttpMethod::Patch:
        return "patch";
    default:
        return "any";
    }
}

std::string failure_mode_to_string(TqFailureMode mode) {
    return (mode == TqFailureMode::FailClosed) ? "fail_closed" : "fail_open";
}

std::string cache_behavior_to_string(TqCacheBehavior behavior) {
    switch (behavior) {
    case TqCacheBehavior::Bypass:
        return "bypass";
    case TqCacheBehavior::Store:
        return "store";
    default:
        return "default";
    }
}

std::string field_filter_mode_to_string(TqFieldFilterMode mode) {
    switch (mode) {
    case TqFieldFilterMode::Allowlist:
        return "allowlist";
    case TqFieldFilterMode::Denylist:
        return "denylist";
    default:
        return "none";
    }
}

std::string pagination_mode_to_string(TqPaginationMode mode) {
    switch (mode) {
    case TqPaginationMode::LimitOffset:
        return "limit_offset";
    case TqPaginationMode::Cursor:
        return "cursor";
    default:
        return "none";
    }
}

std::string already_encoded_behavior_to_string(TqAlreadyEncodedBehavior behavior) {
    return (behavior == TqAlreadyEncodedBehavior::Passthrough) ? "passthrough" : "skip";
}

std::string compression_algorithm_to_string(TqCompressionAlgorithm algo) {
    switch (algo) {
    case TqCompressionAlgorithm::Gzip:
        return "gzip";
    case TqCompressionAlgorithm::Brotli:
        return "brotli";
    case TqCompressionAlgorithm::Zstd:
        return "zstd";
    default:
        return "none";
    }
}

std::string coalescing_mode_to_string(TqCoalescingMode) {
    return "cache_assisted";
}

std::string list_to_string(const std::vector<TqCompressionAlgorithm>& vec) {
    std::string s = "[";
    for (std::size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) {
            s += ", ";
        }
        s += compression_algorithm_to_string(vec[i]);
    }
    s += "]";
    return s;
}

std::string list_to_string(const std::vector<std::string>& vec) {
    std::string s = "[";
    for (std::size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) {
            s += ", ";
        }
        s += vec[i];
    }
    s += "]";
    return s;
}

void add_diff(PolicyIrDiff& diff, const std::string& route_id, const std::string& field_path,
              const std::string& expected, const std::string& actual) {
    PolicyIrFieldDiff fd;
    fd.route_id = route_id;
    fd.field_path = field_path;
    fd.expected = expected;
    fd.actual = actual;
    fd.hint = "YAML loader or YAML-to-IR adapter does not preserve " + field_path;
    diff.field_diffs.push_back(fd);
}

} // namespace

PolicyIrDiff compare_policy_ir(const TqPolicyDocument& expected, const TqPolicyDocument& actual) {
    PolicyIrDiff diff;

    if (expected.schema_version != actual.schema_version) {
        add_diff(diff, "<document>", "schema_version", expected.schema_version,
                 actual.schema_version);
    }
    if (expected.document_id != actual.document_id) {
        add_diff(diff, "<document>", "document_id", expected.document_id, actual.document_id);
    }
    if (expected.source_name != actual.source_name) {
        add_diff(diff, "<document>", "source_name", expected.source_name, actual.source_name);
    }
    if (expected.expected_base_sha != actual.expected_base_sha) {
        add_diff(diff, "<document>", "expected_base_sha", expected.expected_base_sha,
                 actual.expected_base_sha);
    }
    if (expected.routes.size() != actual.routes.size()) {
        add_diff(diff, "<document>", "routes", std::to_string(expected.routes.size()),
                 std::to_string(actual.routes.size()));
    }

    std::size_t min_routes = std::min(expected.routes.size(), actual.routes.size());
    for (std::size_t i = 0; i < min_routes; ++i) {
        const auto& exp_r = expected.routes[i];
        const auto& act_r = actual.routes[i];
        std::string r_id = exp_r.route_id;

        if (exp_r.route_id != act_r.route_id) {
            add_diff(diff, r_id, "route_id", exp_r.route_id, act_r.route_id);
        }
        if (exp_r.match_prefix != act_r.match_prefix) {
            add_diff(diff, r_id, "match_prefix", exp_r.match_prefix, act_r.match_prefix);
        }
        if (exp_r.match_kind != act_r.match_kind) {
            add_diff(diff, r_id, "match_kind", match_kind_to_string(exp_r.match_kind),
                     match_kind_to_string(act_r.match_kind));
        }
        if (exp_r.mutation != act_r.mutation) {
            add_diff(diff, r_id, "mutation", mutation_mode_to_string(exp_r.mutation),
                     mutation_mode_to_string(act_r.mutation));
        }
        if (exp_r.allowed_method != act_r.allowed_method) {
            add_diff(diff, r_id, "allowed_method", http_method_to_string(exp_r.allowed_method),
                     http_method_to_string(act_r.allowed_method));
        }
        if (exp_r.max_response_bytes != act_r.max_response_bytes) {
            add_diff(diff, r_id, "max_response_bytes", std::to_string(exp_r.max_response_bytes),
                     std::to_string(act_r.max_response_bytes));
        }
        if (exp_r.failure_mode != act_r.failure_mode) {
            add_diff(diff, r_id, "failure_mode", failure_mode_to_string(exp_r.failure_mode),
                     failure_mode_to_string(act_r.failure_mode));
        }

        // Cache Policy
        if (exp_r.cache.enabled != act_r.cache.enabled) {
            add_diff(diff, r_id, "cache.enabled", bool_to_string(exp_r.cache.enabled),
                     bool_to_string(act_r.cache.enabled));
        }
        if (exp_r.cache.behavior != act_r.cache.behavior) {
            add_diff(diff, r_id, "cache.behavior", cache_behavior_to_string(exp_r.cache.behavior),
                     cache_behavior_to_string(act_r.cache.behavior));
        }
        if (exp_r.cache.ttl_ms != act_r.cache.ttl_ms) {
            add_diff(diff, r_id, "cache.ttl_ms", std::to_string(exp_r.cache.ttl_ms),
                     std::to_string(act_r.cache.ttl_ms));
        }

        // Cache layers L1
        if (exp_r.cache.l1.enabled != act_r.cache.l1.enabled) {
            add_diff(diff, r_id, "cache.l1.enabled", bool_to_string(exp_r.cache.l1.enabled),
                     bool_to_string(act_r.cache.l1.enabled));
        }
        if (exp_r.cache.l1.capacity_entries != act_r.cache.l1.capacity_entries) {
            add_diff(diff, r_id, "cache.l1.capacity_entries",
                     std::to_string(exp_r.cache.l1.capacity_entries),
                     std::to_string(act_r.cache.l1.capacity_entries));
        }

        // Cache layers L2
        if (exp_r.cache.l2.enabled != act_r.cache.l2.enabled) {
            add_diff(diff, r_id, "cache.l2.enabled", bool_to_string(exp_r.cache.l2.enabled),
                     bool_to_string(act_r.cache.l2.enabled));
        }
        if (exp_r.cache.l2.path != act_r.cache.l2.path) {
            add_diff(diff, r_id, "cache.l2.path", exp_r.cache.l2.path, act_r.cache.l2.path);
        }

        // Private Cache
        if (exp_r.cache.private_cache.enabled != act_r.cache.private_cache.enabled) {
            add_diff(diff, r_id, "cache.private_cache.enabled",
                     bool_to_string(exp_r.cache.private_cache.enabled),
                     bool_to_string(act_r.cache.private_cache.enabled));
        }
        if (exp_r.cache.private_cache.auth_scope_header !=
            act_r.cache.private_cache.auth_scope_header) {
            add_diff(diff, r_id, "cache.private_cache.auth_scope_header",
                     exp_r.cache.private_cache.auth_scope_header,
                     act_r.cache.private_cache.auth_scope_header);
        }

        // Field Variant
        if (exp_r.cache.field_variant.enabled != act_r.cache.field_variant.enabled) {
            add_diff(diff, r_id, "cache.field_variant.enabled",
                     bool_to_string(exp_r.cache.field_variant.enabled),
                     bool_to_string(act_r.cache.field_variant.enabled));
        }
        if (exp_r.cache.field_variant.max_variants_per_route !=
            act_r.cache.field_variant.max_variants_per_route) {
            add_diff(diff, r_id, "cache.field_variant.max_variants_per_route",
                     std::to_string(exp_r.cache.field_variant.max_variants_per_route),
                     std::to_string(act_r.cache.field_variant.max_variants_per_route));
        }
        if (exp_r.cache.field_variant.min_field_count !=
            act_r.cache.field_variant.min_field_count) {
            add_diff(diff, r_id, "cache.field_variant.min_field_count",
                     std::to_string(exp_r.cache.field_variant.min_field_count),
                     std::to_string(act_r.cache.field_variant.min_field_count));
        }
        if (exp_r.cache.field_variant.max_field_count !=
            act_r.cache.field_variant.max_field_count) {
            add_diff(diff, r_id, "cache.field_variant.max_field_count",
                     std::to_string(exp_r.cache.field_variant.max_field_count),
                     std::to_string(act_r.cache.field_variant.max_field_count));
        }
        if (exp_r.cache.field_variant.admission_threshold !=
            act_r.cache.field_variant.admission_threshold) {
            add_diff(diff, r_id, "cache.field_variant.admission_threshold",
                     std::to_string(exp_r.cache.field_variant.admission_threshold),
                     std::to_string(act_r.cache.field_variant.admission_threshold));
        }
        if (exp_r.cache.field_variant.ttl_max_ms != act_r.cache.field_variant.ttl_max_ms) {
            add_diff(diff, r_id, "cache.field_variant.ttl_max_ms",
                     std::to_string(exp_r.cache.field_variant.ttl_max_ms),
                     std::to_string(act_r.cache.field_variant.ttl_max_ms));
        }

        // Vary headers (vector comparison)
        if (exp_r.cache.vary_headers.names != act_r.cache.vary_headers.names) {
            add_diff(diff, r_id, "cache.vary_headers.names",
                     list_to_string(exp_r.cache.vary_headers.names),
                     list_to_string(act_r.cache.vary_headers.names));
        }

        // Field Filter
        if (exp_r.field_filter.mode != act_r.field_filter.mode) {
            add_diff(diff, r_id, "field_filter.mode",
                     field_filter_mode_to_string(exp_r.field_filter.mode),
                     field_filter_mode_to_string(act_r.field_filter.mode));
        }
        if (exp_r.field_filter.fields != act_r.field_filter.fields) {
            add_diff(diff, r_id, "field_filter.fields", list_to_string(exp_r.field_filter.fields),
                     list_to_string(act_r.field_filter.fields));
        }

        // Pagination
        if (exp_r.pagination.enabled != act_r.pagination.enabled) {
            add_diff(diff, r_id, "pagination.enabled", bool_to_string(exp_r.pagination.enabled),
                     bool_to_string(act_r.pagination.enabled));
        }
        if (exp_r.pagination.mode != act_r.pagination.mode) {
            add_diff(diff, r_id, "pagination.mode",
                     pagination_mode_to_string(exp_r.pagination.mode),
                     pagination_mode_to_string(act_r.pagination.mode));
        }
        if (exp_r.pagination.limit_param != act_r.pagination.limit_param) {
            add_diff(diff, r_id, "pagination.limit_param", exp_r.pagination.limit_param,
                     act_r.pagination.limit_param);
        }
        if (exp_r.pagination.offset_param != act_r.pagination.offset_param) {
            add_diff(diff, r_id, "pagination.offset_param", exp_r.pagination.offset_param,
                     act_r.pagination.offset_param);
        }
        if (exp_r.pagination.default_limit != act_r.pagination.default_limit) {
            add_diff(diff, r_id, "pagination.default_limit",
                     std::to_string(exp_r.pagination.default_limit),
                     std::to_string(act_r.pagination.default_limit));
        }
        if (exp_r.pagination.max_limit != act_r.pagination.max_limit) {
            add_diff(diff, r_id, "pagination.max_limit", std::to_string(exp_r.pagination.max_limit),
                     std::to_string(act_r.pagination.max_limit));
        }
        if (exp_r.pagination.upstream_supports_pagination !=
            act_r.pagination.upstream_supports_pagination) {
            add_diff(diff, r_id, "pagination.upstream_supports_pagination",
                     bool_to_string(exp_r.pagination.upstream_supports_pagination),
                     bool_to_string(act_r.pagination.upstream_supports_pagination));
        }
        if (exp_r.pagination.max_response_bytes_warning !=
            act_r.pagination.max_response_bytes_warning) {
            add_diff(diff, r_id, "pagination.max_response_bytes_warning",
                     std::to_string(exp_r.pagination.max_response_bytes_warning),
                     std::to_string(act_r.pagination.max_response_bytes_warning));
        }

        // Compression
        if (exp_r.compression.enabled != act_r.compression.enabled) {
            add_diff(diff, r_id, "compression.enabled", bool_to_string(exp_r.compression.enabled),
                     bool_to_string(act_r.compression.enabled));
        }
        if (exp_r.compression.min_size_bytes != act_r.compression.min_size_bytes) {
            add_diff(diff, r_id, "compression.min_size_bytes",
                     std::to_string(exp_r.compression.min_size_bytes),
                     std::to_string(act_r.compression.min_size_bytes));
        }
        if (exp_r.compression.eligible_content_types != act_r.compression.eligible_content_types) {
            add_diff(diff, r_id, "compression.eligible_content_types",
                     list_to_string(exp_r.compression.eligible_content_types),
                     list_to_string(act_r.compression.eligible_content_types));
        }
        if (exp_r.compression.preferred_algorithms != act_r.compression.preferred_algorithms) {
            add_diff(diff, r_id, "compression.preferred_algorithms",
                     list_to_string(exp_r.compression.preferred_algorithms),
                     list_to_string(act_r.compression.preferred_algorithms));
        }
        if (exp_r.compression.already_encoded_behavior !=
            act_r.compression.already_encoded_behavior) {
            add_diff(
                diff, r_id, "compression.already_encoded_behavior",
                already_encoded_behavior_to_string(exp_r.compression.already_encoded_behavior),
                already_encoded_behavior_to_string(act_r.compression.already_encoded_behavior));
        }

        // Coalescing
        if (exp_r.coalescing.enabled != act_r.coalescing.enabled) {
            add_diff(diff, r_id, "coalescing.enabled", bool_to_string(exp_r.coalescing.enabled),
                     bool_to_string(act_r.coalescing.enabled));
        }
        if (exp_r.coalescing.mode != act_r.coalescing.mode) {
            add_diff(diff, r_id, "coalescing.mode",
                     coalescing_mode_to_string(exp_r.coalescing.mode),
                     coalescing_mode_to_string(act_r.coalescing.mode));
        }
        if (exp_r.coalescing.backend_timeout_ms != act_r.coalescing.backend_timeout_ms) {
            add_diff(diff, r_id, "coalescing.backend_timeout_ms",
                     std::to_string(exp_r.coalescing.backend_timeout_ms),
                     std::to_string(act_r.coalescing.backend_timeout_ms));
        }
        if (exp_r.coalescing.handoff_buffer_ms != act_r.coalescing.handoff_buffer_ms) {
            add_diff(diff, r_id, "coalescing.handoff_buffer_ms",
                     std::to_string(exp_r.coalescing.handoff_buffer_ms),
                     std::to_string(act_r.coalescing.handoff_buffer_ms));
        }
        if (exp_r.coalescing.result_ready_retention_ms !=
            act_r.coalescing.result_ready_retention_ms) {
            add_diff(diff, r_id, "coalescing.result_ready_retention_ms",
                     std::to_string(exp_r.coalescing.result_ready_retention_ms),
                     std::to_string(act_r.coalescing.result_ready_retention_ms));
        }
        if (exp_r.coalescing.max_waiters_per_key != act_r.coalescing.max_waiters_per_key) {
            add_diff(diff, r_id, "coalescing.max_waiters_per_key",
                     std::to_string(exp_r.coalescing.max_waiters_per_key),
                     std::to_string(act_r.coalescing.max_waiters_per_key));
        }
        if (exp_r.coalescing.require_cache_enabled != act_r.coalescing.require_cache_enabled) {
            add_diff(diff, r_id, "coalescing.require_cache_enabled",
                     bool_to_string(exp_r.coalescing.require_cache_enabled),
                     bool_to_string(act_r.coalescing.require_cache_enabled));
        }
        if (exp_r.coalescing.allow_authenticated != act_r.coalescing.allow_authenticated) {
            add_diff(diff, r_id, "coalescing.allow_authenticated",
                     bool_to_string(exp_r.coalescing.allow_authenticated),
                     bool_to_string(act_r.coalescing.allow_authenticated));
        }
        if (exp_r.coalescing.max_follower_wait_budget_ms !=
            act_r.coalescing.max_follower_wait_budget_ms) {
            add_diff(diff, r_id, "coalescing.max_follower_wait_budget_ms",
                     std::to_string(exp_r.coalescing.max_follower_wait_budget_ms),
                     std::to_string(act_r.coalescing.max_follower_wait_budget_ms));
        }
        if (exp_r.coalescing.max_active_follower_waiters !=
            act_r.coalescing.max_active_follower_waiters) {
            add_diff(diff, r_id, "coalescing.max_active_follower_waiters",
                     std::to_string(exp_r.coalescing.max_active_follower_waiters),
                     std::to_string(act_r.coalescing.max_active_follower_waiters));
        }
        if (exp_r.coalescing.max_active_follower_waiters_per_shard !=
            act_r.coalescing.max_active_follower_waiters_per_shard) {
            add_diff(diff, r_id, "coalescing.max_active_follower_waiters_per_shard",
                     std::to_string(exp_r.coalescing.max_active_follower_waiters_per_shard),
                     std::to_string(act_r.coalescing.max_active_follower_waiters_per_shard));
        }
    }

    diff.equal = diff.field_diffs.empty();
    return diff;
}

} // namespace bytetaper::taperquery

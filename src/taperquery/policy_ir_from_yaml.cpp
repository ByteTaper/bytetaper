// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_from_yaml.h"

#include "policy/yaml_loader.h"
#include "taperquery/policy_ir_version.h"

#include <yaml-cpp/yaml.h>

namespace bytetaper::taperquery {

TqRoutePolicy from_runtime_route_policy(const policy::RoutePolicy& route) {
    TqRoutePolicy result;
    if (route.route_id) {
        result.route_id = route.route_id;
    }
    if (route.match_prefix) {
        result.match_prefix = route.match_prefix;
    }
    result.match_kind = static_cast<TqRouteMatchKind>(route.match_kind);
    result.mutation = static_cast<TqMutationMode>(route.mutation);
    result.allowed_method = static_cast<TqHttpMethod>(route.allowed_method);

    // field_filter
    result.field_filter.mode = static_cast<TqFieldFilterMode>(route.field_filter.mode);
    result.field_filter.fields.clear();
    for (std::size_t i = 0; i < route.field_filter.field_count; ++i) {
        result.field_filter.fields.push_back(route.field_filter.fields[i]);
    }

    result.max_response_bytes = route.max_response_bytes;

    // cache
    result.cache.behavior = static_cast<TqCacheBehavior>(route.cache.behavior);
    result.cache.ttl_ms = static_cast<TqDurationMs>(route.cache.ttl_seconds) * 1000u;
    result.cache.enabled = route.cache.enabled;
    result.cache.l1.enabled = route.cache.l1.enabled;
    result.cache.l1.capacity_entries = route.cache.l1.capacity_entries;
    result.cache.l2.enabled = route.cache.l2.enabled;
    result.cache.l2.path = route.cache.l2.path;
    result.cache.private_cache.enabled = route.cache.private_cache;
    result.cache.private_cache.auth_scope_header = route.cache.auth_scope_header;

    result.cache.field_variant.enabled = route.cache.field_variant.enabled;
    result.cache.field_variant.max_variants_per_route =
        route.cache.field_variant.max_variants_per_route;
    result.cache.field_variant.min_field_count = route.cache.field_variant.min_field_count;
    result.cache.field_variant.max_field_count = route.cache.field_variant.max_field_count;
    result.cache.field_variant.admission_threshold = route.cache.field_variant.admission_threshold;
    result.cache.field_variant.ttl_max_ms = route.cache.field_variant.ttl_max_ms;

    result.cache.vary_headers.names.clear();
    for (std::size_t i = 0; i < route.cache.vary_headers.count; ++i) {
        result.cache.vary_headers.names.push_back(route.cache.vary_headers.names[i]);
    }

    result.cache.invalidation.enabled = route.cache.invalidation.enabled;
    result.cache.invalidation.on_methods.clear();
    if (route.cache.invalidation.on_patch)
        result.cache.invalidation.on_methods.push_back("PATCH");
    if (route.cache.invalidation.on_put)
        result.cache.invalidation.on_methods.push_back("PUT");
    if (route.cache.invalidation.on_delete)
        result.cache.invalidation.on_methods.push_back("DELETE");
    if (route.cache.invalidation.timing ==
        policy::CacheInvalidationTiming::AfterSuccessfulUpstreamResponse) {
        result.cache.invalidation.timing = "after_successful_upstream_response";
    }
    result.cache.invalidation.success_status_min = route.cache.invalidation.success_status_min;
    result.cache.invalidation.success_status_max = route.cache.invalidation.success_status_max;
    result.cache.invalidation.targets.clear();
    for (std::size_t i = 0; i < route.cache.invalidation.target_count; ++i) {
        TqCacheInvalidationTarget tgt;
        tgt.route_id = route.cache.invalidation.targets[i].route_id;
        tgt.strategy =
            static_cast<TqCacheInvalidationStrategy>(route.cache.invalidation.targets[i].strategy);
        result.cache.invalidation.targets.push_back(tgt);
    }

    result.failure_mode = static_cast<TqFailureMode>(route.failure_mode);

    // pagination
    result.pagination.enabled = route.pagination.enabled;
    result.pagination.mode = static_cast<TqPaginationMode>(route.pagination.mode);
    result.pagination.limit_param = route.pagination.limit_param;
    result.pagination.offset_param = route.pagination.offset_param;
    result.pagination.default_limit = route.pagination.default_limit;
    result.pagination.max_limit = route.pagination.max_limit;
    result.pagination.upstream_supports_pagination = route.pagination.upstream_supports_pagination;
    result.pagination.max_response_bytes_warning = route.pagination.max_response_bytes_warning;

    // compression
    result.compression.enabled = route.compression.enabled;
    result.compression.min_size_bytes = route.compression.min_size_bytes;
    result.compression.eligible_content_types.clear();
    for (std::size_t i = 0; i < route.compression.eligible_content_type_count; ++i) {
        result.compression.eligible_content_types.push_back(
            route.compression.eligible_content_types[i]);
    }
    result.compression.preferred_algorithms.clear();
    for (std::size_t i = 0; i < route.compression.preferred_algorithm_count; ++i) {
        result.compression.preferred_algorithms.push_back(
            static_cast<TqCompressionAlgorithm>(route.compression.preferred_algorithms[i]));
    }
    result.compression.already_encoded_behavior =
        static_cast<TqAlreadyEncodedBehavior>(route.compression.already_encoded_behavior);

    // coalescing
    result.coalescing.enabled = route.coalescing.enabled;
    result.coalescing.mode = static_cast<TqCoalescingMode>(route.coalescing.mode);
    result.coalescing.backend_timeout_ms = route.coalescing.backend_timeout_ms;
    result.coalescing.handoff_buffer_ms = route.coalescing.handoff_buffer_ms;
    result.coalescing.result_ready_retention_ms = route.coalescing.result_ready_retention_ms;
    result.coalescing.max_waiters_per_key = route.coalescing.max_waiters_per_key;
    result.coalescing.require_cache_enabled = route.coalescing.require_cache_enabled;
    result.coalescing.allow_authenticated = route.coalescing.allow_authenticated;
    result.coalescing.max_follower_wait_budget_ms = route.coalescing.max_follower_wait_budget_ms;
    result.coalescing.max_active_follower_waiters = route.coalescing.max_active_follower_waiters;
    result.coalescing.max_active_follower_waiters_per_shard =
        route.coalescing.max_active_follower_waiters_per_shard;

    return result;
}

PolicyIrLoadResult load_policy_ir_from_yaml_file(const char* path) {
    PolicyIrLoadResult res;
    policy::PolicyFileResult file_res;

    if (!policy::load_policy_from_file(path, &file_res)) {
        res.ok = false;
        res.error = file_res.error ? file_res.error : "Unknown YAML load error";
        return res;
    }

    res.ok = true;
    res.policy.source_name = path;
    res.policy.version.source_schema_version = "yaml/v1";
    res.policy.schema_version = "yaml/v1";
    res.policy.version.policy_ir_version = kCurrentPolicyIrVersion;
    res.policy.version.identity_version = kCurrentPolicyIdentityVersion;

    try {
        YAML::Node root = YAML::LoadFile(path);
        if (root["document_id"] && root["document_id"].IsScalar()) {
            res.policy.document_id = root["document_id"].as<std::string>();
        }
        if (root["expected_base_sha"] && root["expected_base_sha"].IsScalar()) {
            res.policy.expected_base_sha = root["expected_base_sha"].as<std::string>();
        }
    } catch (...) {
        // Ignore parsing errors for non-existent or malformed root elements, fallback to empty
        // defaults
    }

    res.policy.routes.clear();
    for (std::size_t i = 0; i < file_res.count; ++i) {
        res.policy.routes.push_back(from_runtime_route_policy(file_res.policies[i]));
    }

    return res;
}

} // namespace bytetaper::taperquery

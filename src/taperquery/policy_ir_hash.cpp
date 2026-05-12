// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_hash.h"

#include "hash/hash.h"

#include <cstdio>

namespace bytetaper::taperquery {

namespace {

void feed_string(std::string& canonical, const char* label, const std::string& value) {
    canonical += label;
    canonical += ":";
    canonical += value;
    canonical += "|";
}

void feed_uint32(std::string& canonical, const char* label, std::uint32_t value) {
    canonical += label;
    canonical += ":";
    canonical += std::to_string(value);
    canonical += "|";
}

void feed_bool(std::string& canonical, const char* label, bool value) {
    canonical += label;
    canonical += ":";
    canonical += (value ? "true" : "false");
    canonical += "|";
}

template <typename EnumT> void feed_enum(std::string& canonical, const char* label, EnumT value) {
    feed_uint32(canonical, label, static_cast<std::uint32_t>(value));
}

void feed_string_vector(std::string& canonical, const char* label,
                        const std::vector<std::string>& vec) {
    feed_uint32(canonical, (std::string(label) + ".count").c_str(),
                static_cast<std::uint32_t>(vec.size()));
    for (std::size_t i = 0; i < vec.size(); ++i) {
        feed_string(canonical, (std::string(label) + "." + std::to_string(i)).c_str(), vec[i]);
    }
}

template <typename EnumT>
void feed_enum_vector(std::string& canonical, const char* label, const std::vector<EnumT>& vec) {
    feed_uint32(canonical, (std::string(label) + ".count").c_str(),
                static_cast<std::uint32_t>(vec.size()));
    for (std::size_t i = 0; i < vec.size(); ++i) {
        feed_enum(canonical, (std::string(label) + "." + std::to_string(i)).c_str(), vec[i]);
    }
}

std::string format_hex(std::uint64_t hash_val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash_val));
    return std::string(buf);
}

const hash::HashSeed kTqIrHashSeed = { 0x54510001ULL, 0x49520001ULL };

} // namespace

std::string compute_route_policy_identity(const TqRoutePolicy& route) {
    std::string canonical;

    feed_string(canonical, "route_id", route.route_id);
    feed_string(canonical, "match_prefix", route.match_prefix);
    feed_enum(canonical, "match_kind", route.match_kind);
    feed_enum(canonical, "mutation", route.mutation);
    feed_enum(canonical, "allowed_method", route.allowed_method);

    // field_filter
    feed_enum(canonical, "field_filter.mode", route.field_filter.mode);
    feed_string_vector(canonical, "field_filter.fields", route.field_filter.fields);

    feed_uint32(canonical, "max_response_bytes", route.max_response_bytes);

    // cache
    feed_enum(canonical, "cache.behavior", route.cache.behavior);
    feed_uint32(canonical, "cache.ttl_ms", route.cache.ttl_ms);
    feed_bool(canonical, "cache.enabled", route.cache.enabled);
    feed_bool(canonical, "cache.l1.enabled", route.cache.l1.enabled);
    feed_uint32(canonical, "cache.l1.capacity_entries", route.cache.l1.capacity_entries);
    feed_bool(canonical, "cache.l2.enabled", route.cache.l2.enabled);
    feed_string(canonical, "cache.l2.path", route.cache.l2.path);
    feed_bool(canonical, "cache.private_cache.enabled", route.cache.private_cache.enabled);
    feed_string(canonical, "cache.private_cache.auth_scope_header",
                route.cache.private_cache.auth_scope_header);

    feed_bool(canonical, "cache.field_variant.enabled", route.cache.field_variant.enabled);
    feed_uint32(canonical, "cache.field_variant.max_variants_per_route",
                route.cache.field_variant.max_variants_per_route);
    feed_uint32(canonical, "cache.field_variant.min_field_count",
                route.cache.field_variant.min_field_count);
    feed_uint32(canonical, "cache.field_variant.max_field_count",
                route.cache.field_variant.max_field_count);
    feed_uint32(canonical, "cache.field_variant.admission_threshold",
                route.cache.field_variant.admission_threshold);
    feed_uint32(canonical, "cache.field_variant.ttl_max_ms", route.cache.field_variant.ttl_max_ms);

    feed_string_vector(canonical, "cache.vary_headers.names", route.cache.vary_headers.names);

    feed_enum(canonical, "failure_mode", route.failure_mode);

    // pagination
    feed_bool(canonical, "pagination.enabled", route.pagination.enabled);
    feed_enum(canonical, "pagination.mode", route.pagination.mode);
    feed_string(canonical, "pagination.limit_param", route.pagination.limit_param);
    feed_string(canonical, "pagination.offset_param", route.pagination.offset_param);
    feed_uint32(canonical, "pagination.default_limit", route.pagination.default_limit);
    feed_uint32(canonical, "pagination.max_limit", route.pagination.max_limit);
    feed_bool(canonical, "pagination.upstream_supports_pagination",
              route.pagination.upstream_supports_pagination);
    feed_uint32(canonical, "pagination.max_response_bytes_warning",
                route.pagination.max_response_bytes_warning);

    // compression
    feed_bool(canonical, "compression.enabled", route.compression.enabled);
    feed_uint32(canonical, "compression.min_size_bytes", route.compression.min_size_bytes);
    feed_string_vector(canonical, "compression.eligible_content_types",
                       route.compression.eligible_content_types);
    feed_enum_vector(canonical, "compression.preferred_algorithms",
                     route.compression.preferred_algorithms);
    feed_enum(canonical, "compression.already_encoded_behavior",
              route.compression.already_encoded_behavior);

    // coalescing
    feed_bool(canonical, "coalescing.enabled", route.coalescing.enabled);
    feed_enum(canonical, "coalescing.mode", route.coalescing.mode);
    feed_uint32(canonical, "coalescing.backend_timeout_ms", route.coalescing.backend_timeout_ms);
    feed_uint32(canonical, "coalescing.handoff_buffer_ms", route.coalescing.handoff_buffer_ms);
    feed_uint32(canonical, "coalescing.result_ready_retention_ms",
                route.coalescing.result_ready_retention_ms);
    feed_uint32(canonical, "coalescing.max_waiters_per_key", route.coalescing.max_waiters_per_key);
    feed_bool(canonical, "coalescing.require_cache_enabled",
              route.coalescing.require_cache_enabled);
    feed_bool(canonical, "coalescing.allow_authenticated", route.coalescing.allow_authenticated);
    feed_uint32(canonical, "coalescing.max_follower_wait_budget_ms",
                route.coalescing.max_follower_wait_budget_ms);
    feed_uint32(canonical, "coalescing.max_active_follower_waiters",
                route.coalescing.max_active_follower_waiters);
    feed_uint32(canonical, "coalescing.max_active_follower_waiters_per_shard",
                route.coalescing.max_active_follower_waiters_per_shard);

    std::uint64_t hash_val =
        hash::siphash24_bytes(canonical.data(), canonical.size(), kTqIrHashSeed);
    return format_hex(hash_val);
}

std::string compute_policy_document_identity(const TqPolicyDocument& policy) {
    std::string canonical;

    feed_string(canonical, "schema_version", policy.schema_version);
    feed_string(canonical, "document_id", policy.document_id);
    feed_string(canonical, "source_name", policy.source_name);
    feed_string(canonical, "expected_base_sha", policy.expected_base_sha);

    feed_uint32(canonical, "routes.count", static_cast<std::uint32_t>(policy.routes.size()));
    for (std::size_t i = 0; i < policy.routes.size(); ++i) {
        feed_string(canonical, ("route." + std::to_string(i)).c_str(),
                    compute_route_policy_identity(policy.routes[i]));
    }

    std::uint64_t hash_val =
        hash::siphash24_bytes(canonical.data(), canonical.size(), kTqIrHashSeed);
    return format_hex(hash_val);
}

} // namespace bytetaper::taperquery

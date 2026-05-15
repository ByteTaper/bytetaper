// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/policy_identity.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace bytetaper::policy {

namespace {

class PolicyIdentityWriter {
public:
    PolicyIdentityWriter() {
        h_a = 14695981039346656037ULL; // FNV-1a 64-bit offset basis A
        h_b = 5615783353457913349ULL;  // Offset basis B
    }

    void feed_byte(uint8_t b) {
        h_a ^= b;
        h_a *= 1099511628211ULL; // FNV-1a 64-bit prime

        h_b ^= b;
        h_b *= 1099511628211ULL;
    }

    void feed_bytes(const void* data, std::size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < len; ++i) {
            feed_byte(p[i]);
        }
    }

    void feed_section(const char* name) {
        feed_byte(0x01);
        feed_string(name);
    }

    void feed_tag(const char* name) {
        feed_byte(0x02);
        feed_string(name);
    }

    void feed_bool(bool v) {
        feed_byte(v ? 1 : 0);
    }

    void feed_u8(uint8_t v) {
        feed_byte(v);
    }

    void feed_u32(uint32_t v) {
        feed_byte(v & 0xFF);
        feed_byte((v >> 8) & 0xFF);
        feed_byte((v >> 16) & 0xFF);
        feed_byte((v >> 24) & 0xFF);
    }

    void feed_string(const char* s) {
        uint32_t len = s ? static_cast<uint32_t>(std::strlen(s)) : 0u;
        feed_u32(len);
        if (s) {
            feed_bytes(s, len);
        }
    }

    bool finish_route(PolicyIdentityV2* out) const {
        if (!out)
            return false;
        int written = std::snprintf(out->value, kPolicyIdentityV2MaxLen, "btp2:%016llx%016llx",
                                    static_cast<unsigned long long>(h_a),
                                    static_cast<unsigned long long>(h_b));
        return written > 0 && written < static_cast<int>(kPolicyIdentityV2MaxLen);
    }

    bool finish_doc(PolicyIdentityV2* out) const {
        if (!out)
            return false;
        int written = std::snprintf(out->value, kPolicyIdentityV2MaxLen, "btp2-doc:%016llx%016llx",
                                    static_cast<unsigned long long>(h_a),
                                    static_cast<unsigned long long>(h_b));
        return written > 0 && written < static_cast<int>(kPolicyIdentityV2MaxLen);
    }

private:
    std::uint64_t h_a;
    std::uint64_t h_b;
};

} // namespace

bool compute_route_policy_identity_v2(const RoutePolicy& route, PolicyIdentityV2* out) {
    if (!out)
        return false;

    PolicyIdentityWriter writer;

    writer.feed_section("route");
    writer.feed_tag("route_id");
    writer.feed_string(route.route_id);
    writer.feed_tag("match_prefix");
    writer.feed_string(route.match_prefix);
    writer.feed_tag("match_kind");
    writer.feed_u8(static_cast<uint8_t>(route.match_kind));
    writer.feed_tag("mutation");
    writer.feed_u8(static_cast<uint8_t>(route.mutation));
    writer.feed_tag("allowed_method");
    writer.feed_u8(static_cast<uint8_t>(route.allowed_method));
    writer.feed_tag("max_response_bytes");
    writer.feed_u32(route.max_response_bytes);
    writer.feed_tag("failure_mode");
    writer.feed_u8(static_cast<uint8_t>(route.failure_mode));

    // field_filter
    writer.feed_section("field_filter");
    writer.feed_tag("mode");
    writer.feed_u8(static_cast<uint8_t>(route.field_filter.mode));
    writer.feed_tag("field_count");
    writer.feed_u32(static_cast<uint32_t>(route.field_filter.field_count));
    for (std::size_t i = 0; i < route.field_filter.field_count; ++i) {
        writer.feed_tag("field");
        writer.feed_string(route.field_filter.fields[i]);
    }

    // cache
    writer.feed_section("cache");
    writer.feed_tag("behavior");
    writer.feed_u8(static_cast<uint8_t>(route.cache.behavior));
    writer.feed_tag("ttl_seconds");
    writer.feed_u32(route.cache.ttl_seconds);
    writer.feed_tag("enabled");
    writer.feed_bool(route.cache.enabled);
    writer.feed_tag("l1_enabled");
    writer.feed_bool(route.cache.l1.enabled);
    writer.feed_tag("l1_capacity");
    writer.feed_u32(route.cache.l1.capacity_entries);
    writer.feed_tag("l2_enabled");
    writer.feed_bool(route.cache.l2.enabled);
    writer.feed_tag("l2_path");
    writer.feed_string(route.cache.l2.path);
    writer.feed_tag("private_cache");
    writer.feed_bool(route.cache.private_cache);
    writer.feed_tag("auth_scope_header");
    writer.feed_string(route.cache.auth_scope_header);
    writer.feed_tag("field_variant_enabled");
    writer.feed_bool(route.cache.field_variant.enabled);
    writer.feed_tag("field_variant_max_variants");
    writer.feed_u32(route.cache.field_variant.max_variants_per_route);
    writer.feed_tag("field_variant_min_field_count");
    writer.feed_u32(route.cache.field_variant.min_field_count);
    writer.feed_tag("field_variant_max_field_count");
    writer.feed_u32(route.cache.field_variant.max_field_count);
    writer.feed_tag("field_variant_admission_threshold");
    writer.feed_u32(route.cache.field_variant.admission_threshold);
    writer.feed_tag("field_variant_ttl_max_ms");
    writer.feed_u32(route.cache.field_variant.ttl_max_ms);

    // vary_headers
    writer.feed_tag("vary_headers_count");
    writer.feed_u32(static_cast<uint32_t>(route.cache.vary_headers.count));
    for (std::size_t i = 0; i < route.cache.vary_headers.count; ++i) {
        writer.feed_tag("vary_header");
        writer.feed_string(route.cache.vary_headers.names[i]);
    }

    // invalidation
    writer.feed_tag("invalidation_enabled");
    writer.feed_bool(route.cache.invalidation.enabled);
    writer.feed_tag("invalidation_on_patch");
    writer.feed_bool(route.cache.invalidation.on_patch);
    writer.feed_tag("invalidation_on_put");
    writer.feed_bool(route.cache.invalidation.on_put);
    writer.feed_tag("invalidation_on_delete");
    writer.feed_bool(route.cache.invalidation.on_delete);
    writer.feed_tag("invalidation_timing");
    writer.feed_u8(static_cast<uint8_t>(route.cache.invalidation.timing));
    writer.feed_tag("invalidation_success_status_min");
    writer.feed_u32(route.cache.invalidation.success_status_min);
    writer.feed_tag("invalidation_success_status_max");
    writer.feed_u32(route.cache.invalidation.success_status_max);
    writer.feed_tag("invalidation_target_count");
    writer.feed_u32(static_cast<uint32_t>(route.cache.invalidation.target_count));
    for (std::size_t i = 0; i < route.cache.invalidation.target_count; ++i) {
        writer.feed_tag("invalidation_target_route_id");
        writer.feed_string(route.cache.invalidation.targets[i].route_id);
        writer.feed_tag("invalidation_target_strategy");
        writer.feed_u8(static_cast<uint8_t>(route.cache.invalidation.targets[i].strategy));
    }

    // pagination
    writer.feed_section("pagination");
    writer.feed_tag("enabled");
    writer.feed_bool(route.pagination.enabled);
    writer.feed_tag("mode");
    writer.feed_u8(static_cast<uint8_t>(route.pagination.mode));
    writer.feed_tag("limit_param");
    writer.feed_string(route.pagination.limit_param);
    writer.feed_tag("offset_param");
    writer.feed_string(route.pagination.offset_param);
    writer.feed_tag("default_limit");
    writer.feed_u32(route.pagination.default_limit);
    writer.feed_tag("max_limit");
    writer.feed_u32(route.pagination.max_limit);
    writer.feed_tag("upstream_supports_pagination");
    writer.feed_bool(route.pagination.upstream_supports_pagination);
    writer.feed_tag("max_response_bytes_warning");
    writer.feed_u32(route.pagination.max_response_bytes_warning);

    // compression
    writer.feed_section("compression");
    writer.feed_tag("enabled");
    writer.feed_bool(route.compression.enabled);
    writer.feed_tag("min_size_bytes");
    writer.feed_u32(route.compression.min_size_bytes);
    writer.feed_tag("eligible_content_type_count");
    writer.feed_u32(static_cast<uint32_t>(route.compression.eligible_content_type_count));
    for (std::size_t i = 0; i < route.compression.eligible_content_type_count; ++i) {
        writer.feed_tag("eligible_content_type");
        writer.feed_string(route.compression.eligible_content_types[i]);
    }
    writer.feed_tag("preferred_algorithm_count");
    writer.feed_u32(static_cast<uint32_t>(route.compression.preferred_algorithm_count));
    for (std::size_t i = 0; i < route.compression.preferred_algorithm_count; ++i) {
        writer.feed_tag("preferred_algorithm");
        writer.feed_u8(static_cast<uint8_t>(route.compression.preferred_algorithms[i]));
    }
    writer.feed_tag("already_encoded_behavior");
    writer.feed_u8(static_cast<uint8_t>(route.compression.already_encoded_behavior));

    // coalescing
    writer.feed_section("coalescing");
    writer.feed_tag("enabled");
    writer.feed_bool(route.coalescing.enabled);
    writer.feed_tag("mode");
    writer.feed_u8(static_cast<uint8_t>(route.coalescing.mode));
    writer.feed_tag("backend_timeout_ms");
    writer.feed_u32(route.coalescing.backend_timeout_ms);
    writer.feed_tag("handoff_buffer_ms");
    writer.feed_u32(route.coalescing.handoff_buffer_ms);
    writer.feed_tag("result_ready_retention_ms");
    writer.feed_u32(route.coalescing.result_ready_retention_ms);
    writer.feed_tag("max_waiters_per_key");
    writer.feed_u32(route.coalescing.max_waiters_per_key);
    writer.feed_tag("require_cache_enabled");
    writer.feed_bool(route.coalescing.require_cache_enabled);
    writer.feed_tag("allow_authenticated");
    writer.feed_bool(route.coalescing.allow_authenticated);
    writer.feed_tag("max_follower_wait_budget_ms");
    writer.feed_u32(route.coalescing.max_follower_wait_budget_ms);
    writer.feed_tag("max_active_follower_waiters");
    writer.feed_u32(route.coalescing.max_active_follower_waiters);
    writer.feed_tag("max_active_follower_waiters_per_shard");
    writer.feed_u32(route.coalescing.max_active_follower_waiters_per_shard);

    return writer.finish_route(out);
}

bool compute_policy_file_identity_v2(const RoutePolicy* routes, std::size_t route_count,
                                     PolicyIdentityV2* out) {
    if (!out) {
        return false;
    }
    if (routes == nullptr && route_count != 0) {
        return false;
    }

    PolicyIdentityWriter writer;

    writer.feed_section("document");
    writer.feed_tag("version");
    writer.feed_string("btp2-doc");
    writer.feed_tag("route_count");
    writer.feed_u32(static_cast<uint32_t>(route_count));

    for (std::size_t i = 0; i < route_count; ++i) {
        PolicyIdentityV2 route_id_v2 = {};
        if (!compute_route_policy_identity_v2(routes[i], &route_id_v2)) {
            return false;
        }
        writer.feed_tag("route_index");
        writer.feed_u32(static_cast<uint32_t>(i));
        writer.feed_tag("route_hash");
        writer.feed_string(route_id_v2.value);
    }

    return writer.finish_doc(out);
}

bool copy_route_policy_identity_v2_to_legacy_slot(RoutePolicy* route) {
    if (!route) {
        return false;
    }
    PolicyIdentityV2 out = {};
    if (!compute_route_policy_identity_v2(*route, &out)) {
        return false;
    }
    std::strncpy(route->policy_identity, out.value, kPolicyIdentityMaxLen - 1);
    route->policy_identity[kPolicyIdentityMaxLen - 1] = '\0';
    return true;
}

} // namespace bytetaper::policy

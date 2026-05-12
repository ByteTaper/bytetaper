// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_H

#include "policy/route_policy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

enum class TqRouteMatchKind : std::uint8_t {
    Prefix = 0,
    Exact = 1,
};

enum class TqMutationMode : std::uint8_t {
    Disabled = 0,
    HeadersOnly = 1,
    Full = 2,
};

enum class TqFailureMode : std::uint8_t {
    FailOpen = 0,
    FailClosed = 1,
};

enum class TqHttpMethod : std::uint8_t {
    Any = 0,
    Get = 1,
    Post = 2,
    Put = 3,
    Delete = 4,
    Patch = 5,
};

enum class TqCacheBehavior : std::uint8_t {
    Default = 0,
    Bypass = 1,
    Store = 2,
};

enum class TqCoalescingMode : std::uint8_t {
    CacheAssisted = 0,
};

enum class TqCompressionAlgorithm : std::uint8_t {
    None = 0,
    Gzip = 1,
    Brotli = 2,
    Zstd = 3,
};

enum class TqAlreadyEncodedBehavior : std::uint8_t {
    Skip = 0,
    Passthrough = 1,
};

enum class TqFieldFilterMode : std::uint8_t {
    None = 0,
    Allowlist = 1,
    Denylist = 2,
};

enum class TqPaginationMode : std::uint8_t {
    None = 0,
    LimitOffset = 1,
    Cursor = 2,
};

using TqDurationMs = std::uint32_t;
using TqSizeBytes = std::uint32_t;
using TqEntryCount = std::uint32_t;

inline TqDurationMs duration_ms(std::uint32_t v) {
    return v;
}
inline TqDurationMs duration_seconds(std::uint32_t v) {
    return v * 1000u;
}
inline TqSizeBytes size_bytes(std::uint32_t v) {
    return v;
}
inline TqSizeBytes size_kib(std::uint32_t v) {
    return v * 1024u;
}
inline TqSizeBytes size_mib(std::uint32_t v) {
    return v * 1024u * 1024u;
}

struct TqFieldFilterPolicy {
    TqFieldFilterMode mode = TqFieldFilterMode::None;
    std::vector<std::string> fields;
};

struct TqFieldVariantCachePolicy {
    bool enabled = false;
    std::uint32_t max_variants_per_route = 8;
    std::uint32_t min_field_count = 1;
    std::uint32_t max_field_count = 8;
    std::uint32_t admission_threshold = 2;
    TqDurationMs ttl_max_ms = 0;
};

struct TqCacheL1Policy {
    bool enabled = false;
    TqEntryCount capacity_entries = 0;
};

struct TqCacheL2Policy {
    bool enabled = false;
    std::string path;
};

struct TqCacheVaryHeaderPolicy {
    std::vector<std::string> names;
};

struct TqPrivateCachePolicy {
    bool enabled = false;
    std::string auth_scope_header;
};

struct TqCachePolicy {
    TqCacheBehavior behavior = TqCacheBehavior::Default;
    TqDurationMs ttl_ms = 0;
    bool enabled = false;
    TqCacheL1Policy l1{};
    TqCacheL2Policy l2{};
    TqPrivateCachePolicy private_cache{};
    TqFieldVariantCachePolicy field_variant{};
    TqCacheVaryHeaderPolicy vary_headers{};
};

struct TqPaginationPolicy {
    bool enabled = false;
    TqPaginationMode mode = TqPaginationMode::None;
    std::string limit_param = "limit";
    std::string offset_param = "offset";
    std::uint32_t default_limit = 0;
    std::uint32_t max_limit = 0;
    bool upstream_supports_pagination = false;
    TqSizeBytes max_response_bytes_warning = 0;
};

struct TqCompressionPolicy {
    bool enabled = false;
    TqSizeBytes min_size_bytes = 0;
    std::vector<std::string> eligible_content_types;
    std::vector<TqCompressionAlgorithm> preferred_algorithms;
    TqAlreadyEncodedBehavior already_encoded_behavior = TqAlreadyEncodedBehavior::Skip;
};

struct TqCoalescingPolicy {
    bool enabled = false;
    TqCoalescingMode mode = TqCoalescingMode::CacheAssisted;
    TqDurationMs backend_timeout_ms = 500;
    TqDurationMs handoff_buffer_ms = 250;
    TqDurationMs result_ready_retention_ms = 50;
    std::uint32_t max_waiters_per_key = 64;
    bool require_cache_enabled = true;
    bool allow_authenticated = false;
    TqDurationMs max_follower_wait_budget_ms = 0;
    std::uint32_t max_active_follower_waiters = 0;
    std::uint32_t max_active_follower_waiters_per_shard = 0;
};

struct TqRoutePolicy {
    std::string route_id;
    std::string match_prefix;
    TqRouteMatchKind match_kind = TqRouteMatchKind::Prefix;
    TqMutationMode mutation = TqMutationMode::Disabled;
    TqHttpMethod allowed_method = TqHttpMethod::Any;
    TqFieldFilterPolicy field_filter{};
    TqSizeBytes max_response_bytes = 0;
    TqCachePolicy cache{};
    TqFailureMode failure_mode = TqFailureMode::FailOpen;
    TqPaginationPolicy pagination{};
    TqCompressionPolicy compression{};
    TqCoalescingPolicy coalescing{};
};

struct TqPolicyDocument {
    std::string schema_version;
    std::string document_id;
    std::string source_name;
    std::string expected_base_sha;
    std::vector<TqRoutePolicy> routes;
};

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_H

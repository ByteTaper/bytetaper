// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CACHE_CACHE_KEY_H
#define BYTETAPER_CACHE_CACHE_KEY_H

#include "policy/cache_policy.h"
#include "policy/field_filter_policy.h"
#include "policy/method_matcher.h"

#include <cstddef>

namespace bytetaper::cache {

struct CacheVaryKeyPart {
    const char* name = nullptr;
    const char* value_hash = nullptr;
};

struct CacheKeyInput {
    policy::HttpMethod method = policy::HttpMethod::Get;
    const char* route_id = nullptr;
    const char* path = nullptr;
    const char* query = nullptr;
    const char (*selected_fields)[policy::kMaxFieldNameLen] = nullptr;
    std::size_t selected_field_count = 0;
    const char* policy_version = nullptr;
    CacheVaryKeyPart vary_headers[policy::kMaxCacheVaryHeaders] = {};
    std::size_t vary_header_count = 0;
    bool private_cache = false;       // when true, auth_scope is required
    const char* auth_scope = nullptr; // opaque derived scope — never the raw token
    bool variant = false;             // when true, emits "var:" namespace prefix
};

// Writes a deterministic null-terminated key into out_buf[out_size].
// Returns false if: method is not Get, route_id or path is null,
// out_buf is null, or the key would overflow out_buf.
// selected_fields are sorted alphabetically and deduplicated.
bool build_cache_key(const CacheKeyInput& input, char* out_buf, std::size_t out_size);

// Strips the ?fields=... parameter from a query string.
// Writes result into out_buf[out_size]. Returns bytes written.
std::size_t sanitize_query_strip_fields_param(const char* query, char* out_buf,
                                              std::size_t out_size);

} // namespace bytetaper::cache

#endif // BYTETAPER_CACHE_CACHE_KEY_H

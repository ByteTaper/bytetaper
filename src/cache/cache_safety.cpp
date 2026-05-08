// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_safety.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace bytetaper::cache {

bool cache_auth_bypass(bool has_authorization, bool has_cookie, bool private_cache_enabled) {
    // If the route explicitly opts in to private caching, we don't bypass.
    if (private_cache_enabled) {
        return false;
    }

    // Default safety: bypass cache if any authentication headers are present.
    return has_authorization || has_cookie;
}

bool build_private_cache_scope_hash(const char* raw_scope, std::size_t raw_scope_len, char* out,
                                    std::size_t out_size) {
    if (raw_scope == nullptr || raw_scope_len == 0 || out == nullptr || out_size < 17) {
        return false;
    }
    std::uint64_t h = 14695981039346656037ULL;
    for (std::size_t i = 0; i < raw_scope_len; ++i) {
        h ^= static_cast<std::uint8_t>(raw_scope[i]);
        h *= 1099511628211ULL;
    }
    std::snprintf(out, out_size, "%016llx", static_cast<unsigned long long>(h));
    return true;
}

} // namespace bytetaper::cache

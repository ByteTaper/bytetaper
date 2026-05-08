// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CACHE_CACHE_SAFETY_H
#define BYTETAPER_CACHE_CACHE_SAFETY_H

#include <cstddef>

namespace bytetaper::cache {

/**
 * Returns true when cache lookup AND store must be skipped for this request.
 *
 * Logic:
 * - If private_cache_enabled is true, we allow caching regardless of auth headers.
 * - Otherwise, if either Authorization or Cookie headers are present, we bypass cache
 *   to avoid serving or storing personalized/authenticated data.
 *
 * @param has_authorization True if Authorization header is present.
 * @param has_cookie True if Cookie header is present.
 * @param private_cache_enabled True if the route policy explicitly allows private caching.
 */
bool cache_auth_bypass(bool has_authorization, bool has_cookie, bool private_cache_enabled);

bool build_private_cache_scope_hash(const char* raw_scope, std::size_t raw_scope_len, char* out,
                                    std::size_t out_size);

bool build_cache_vary_value_hash(const char* raw_value, std::size_t raw_value_len, char* out,
                                 std::size_t out_size);

} // namespace bytetaper::cache

#endif // BYTETAPER_CACHE_CACHE_SAFETY_H

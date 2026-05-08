// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_POLICY_CACHE_POLICY_H
#define BYTETAPER_POLICY_CACHE_POLICY_H

#include "policy/field_filter_policy.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::policy {

enum class CacheBehavior : std::uint8_t {
    Default = 0, // no explicit cache instruction (pass-through)
    Bypass = 1,  // always bypass cache
    Store = 2,   // store response in cache
};

static constexpr std::size_t kMaxCachePathLen = 256;

struct CacheL1Policy {
    bool enabled = false;
    std::uint32_t capacity_entries = 0; // 0 = use kL1SlotCount default
};

struct CacheL2Policy {
    bool enabled = false;
    char path[kMaxCachePathLen] = {}; // required when enabled
};

struct CachePolicy {
    CacheBehavior behavior = CacheBehavior::Default;
    std::uint32_t ttl_seconds = 0;
    bool enabled = false;
    CacheL1Policy l1{};
    CacheL2Policy l2{};
    bool private_cache = false;      // opt-in: allows caching of authenticated requests
    char auth_scope_header[64] = {}; // required when private_cache=true; names the source header
    FieldVariantCachePolicy field_variant{};
};

// Returns nullptr on success, or a static error string on invalid configuration.
const char* validate_cache_policy(const CachePolicy& policy);

} // namespace bytetaper::policy

#endif // BYTETAPER_POLICY_CACHE_POLICY_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CACHE_CACHE_ENTRY_H
#define BYTETAPER_CACHE_CACHE_ENTRY_H

#include <cstddef>
#include <cstdint>

namespace bytetaper::cache {

static constexpr std::size_t kCacheKeyMaxLen = 1024;
static constexpr std::size_t kCacheContentTypeMaxLen = 64;

struct CacheEntry {
    char key[kCacheKeyMaxLen] = {};
    std::uint16_t status_code = 0;
    char content_type[kCacheContentTypeMaxLen] = {};
    const char* body = nullptr; // non-owning; caller manages lifetime
    std::size_t body_len = 0;
    std::size_t original_body_len = 0; // input body size before field filtering; 0 if unfiltered
    std::uint16_t removed_fields = 0;  // fields removed by filter; 0 if unfiltered
    std::int64_t created_at_epoch_ms = 0;
    std::int64_t expires_at_epoch_ms = 0; // 0 = no TTL / never expires
};

// Returns true if now_ms >= entry.expires_at_epoch_ms.
// Returns false when expires_at_epoch_ms == 0 (no TTL set).
bool is_expired(const CacheEntry& entry, std::int64_t now_ms);

} // namespace bytetaper::cache

#endif // BYTETAPER_CACHE_CACHE_ENTRY_H

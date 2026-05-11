// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CACHE_L1_CACHE_H
#define BYTETAPER_CACHE_L1_CACHE_H

#include "cache/cache_entry.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace bytetaper::cache {

static constexpr std::size_t kL1ShardCount = 256;
static constexpr std::size_t kL1SlotsPerShard = 16;
static constexpr std::size_t kL1MaxBodySize = 3072; // 3KiB per slot

struct L1CacheShard {
    mutable std::mutex mutex;
    CacheEntry slots[kL1SlotsPerShard];
    char bodies[kL1SlotsPerShard][kL1MaxBodySize];
    std::uint32_t generations[kL1SlotsPerShard];
    std::uint64_t key_hashes[kL1SlotsPerShard];
    std::size_t write_cursor;
};

struct L1Cache {
    L1CacheShard shards[kL1ShardCount];
};

} // namespace bytetaper::cache

namespace bytetaper::metrics {
struct CacheMetrics;
}

namespace bytetaper::cache {

void l1_init(L1Cache* cache);

// Stores a copy of entry in the appropriate shard.
void l1_put(L1Cache* cache, const CacheEntry& entry,
            bytetaper::metrics::CacheMetrics* metrics = nullptr);

// Promotes entry into L1 only if no newer entry for the same key already exists.
// "Newer" means existing.created_at_epoch_ms > entry.created_at_epoch_ms.
// If the key is not present in L1, promotes unconditionally.
// Returns true if promoted, false if rejected due to staleness.
bool l1_put_if_newer(L1Cache* cache, const CacheEntry& entry,
                     bytetaper::metrics::CacheMetrics* metrics = nullptr);

// Retrieves an entry by key from the appropriate shard.
// Copies the matching entry into *out and copies the body into body_out.
// Returns true on hit. Returns false on miss.
// now_ms == 0 skips expiry check.
bool l1_get(const L1Cache* cache, const char* key, std::int64_t now_ms, CacheEntry* out,
            char* body_out, std::size_t body_out_capacity,
            bytetaper::metrics::CacheMetrics* metrics = nullptr);

// Returns true only if entry is safe to store in L1 (body present and within capacity).
bool l1_can_store_entry(const CacheEntry& entry);

} // namespace bytetaper::cache

#endif // BYTETAPER_CACHE_L1_CACHE_H

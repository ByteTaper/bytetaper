// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"

#include "cache/cache_entry.h"

#include <cstring>

namespace bytetaper::cache {

namespace {

// Standard initial value for DJB2 hashing algorithm.
static constexpr std::size_t kDJB2InitialHash = 5381;

// Simple, fast hash function (DJB2) used when cache is large.
std::uint64_t hash_key(const char* key) {
    if (key == nullptr) {
        return 0;
    }
    std::uint64_t h = kDJB2InitialHash;
    int c;
    while ((c = static_cast<unsigned char>(*key++))) {
        h = ((h << 5) + h) + static_cast<std::uint64_t>(c); // h * 33 + c
    }
    return h;
}

void copy_body_to_slot(L1CacheShard* shard, std::size_t slot_idx, const CacheEntry& entry) {
    if (entry.body == nullptr || entry.body_len == 0) {
        shard->slots[slot_idx].body = nullptr;
        shard->slots[slot_idx].body_len = 0;
        return;
    }

    std::size_t copy_len = (entry.body_len > kL1MaxBodySize) ? kL1MaxBodySize : entry.body_len;
    std::memcpy(shard->bodies[slot_idx], entry.body, copy_len);
    shard->slots[slot_idx].body = shard->bodies[slot_idx];
    shard->slots[slot_idx].body_len = copy_len;
}

} // namespace

bool l1_can_store_entry(const CacheEntry& entry) {
    if (entry.body_len > kL1MaxBodySize) {
        return false;
    }
    if (entry.body_len > 0 && entry.body == nullptr) {
        return false;
    }
    return true;
}

void l1_init(L1Cache* cache) {
    if (cache == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < kL1ShardCount; ++i) {
        auto& shard = cache->shards[i];
        std::memset(shard.slots, 0, sizeof(shard.slots));
        std::memset(shard.bodies, 0, sizeof(shard.bodies));
        std::memset(shard.generations, 0, sizeof(shard.generations));
        std::memset(shard.key_hashes, 0, sizeof(shard.key_hashes));
        shard.write_cursor = 0;
    }
}

void l1_put(L1Cache* cache, const CacheEntry& entry) {
    if (cache == nullptr || entry.key == nullptr) {
        return;
    }
    if (!l1_can_store_entry(entry)) {
        return;
    }

    const std::uint64_t h = hash_key(entry.key);
    const std::size_t shard_idx = h % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    std::scoped_lock lock(shard.mutex);

    // Sequential FIFO within the shard.
    const std::size_t slot_idx = shard.write_cursor % kL1SlotsPerShard;
    shard.slots[slot_idx] = entry;
    copy_body_to_slot(&shard, slot_idx, entry);
    shard.key_hashes[slot_idx] = h;
    shard.generations[slot_idx] += 1;
    shard.write_cursor += 1;
}

bool l1_put_if_newer(L1Cache* cache, const CacheEntry& entry) {
    if (cache == nullptr || entry.key == nullptr) {
        return false;
    }
    if (!l1_can_store_entry(entry)) {
        return false;
    }

    const std::uint64_t h = hash_key(entry.key);
    const std::size_t shard_idx = h % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    std::scoped_lock lock(shard.mutex);

    std::size_t target_slot = shard.write_cursor % kL1SlotsPerShard;
    bool found = false;

    // 1. Check for existing entry to verify staleness
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        if (shard.generations[i] > 0 && shard.key_hashes[i] == h &&
            std::strcmp(shard.slots[i].key, entry.key) == 0) {
            // Found existing entry. Check if it is newer.
            if (shard.slots[i].created_at_epoch_ms > entry.created_at_epoch_ms) {
                return false; // Existing is newer, do not promote stale data.
            }
            // Existing is older or same age, we will overwrite this EXACT slot
            // to avoid having multiple copies of the same key in the ring.
            target_slot = i;
            found = true;
            break;
        }
    }

    // 2. Perform insertion
    shard.slots[target_slot] = entry;
    copy_body_to_slot(&shard, target_slot, entry);
    shard.key_hashes[target_slot] = h;
    shard.generations[target_slot] += 1;

    // Only increment cursor if we didn't overwrite an existing slot.
    // This keeps the ring buffer strictly FIFO for new keys.
    if (!found) {
        shard.write_cursor += 1;
    }
    return true;
}

bool l1_get(const L1Cache* cache, const char* key, std::int64_t now_ms, CacheEntry* out,
            char* body_out, std::size_t body_out_capacity) {
    if (cache == nullptr || key == nullptr || out == nullptr) {
        return false;
    }

    const std::uint64_t h = hash_key(key);
    const std::size_t shard_idx = h % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    std::scoped_lock lock(shard.mutex);

    // Linear Scan within the shard.
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        if (shard.generations[i] == 0) {
            continue;
        }
        if (shard.key_hashes[i] != h) {
            continue;
        }
        if (std::strncmp(shard.slots[i].key, key, kCacheKeyMaxLen) != 0) {
            continue;
        }
        if (now_ms > 0 && is_expired(shard.slots[i], now_ms)) {
            continue;
        }

        *out = shard.slots[i];

        // Copy body into provided buffer while locked to prevent torn reads.
        if (out->body_len > 0 && body_out != nullptr && body_out_capacity > 0) {
            std::size_t copy_len =
                (out->body_len > body_out_capacity) ? body_out_capacity : out->body_len;
            std::memcpy(body_out, shard.bodies[i], copy_len);
            out->body = body_out;
            out->body_len = copy_len;
        } else {
            out->body = nullptr;
            out->body_len = 0;
        }
        return true;
    }
    return false;
}

} // namespace bytetaper::cache

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"

#include "cache/cache_entry.h"
#include "hash/hash.h"
#include "metrics/cache_metrics.h"

#include <cstring>

namespace bytetaper::cache {

namespace {

void copy_body_to_slot(L1CacheShard* shard, std::size_t slot_idx, const CacheEntry& entry) {
    std::memset(shard->bodies[slot_idx], 0, kL1MaxBodySize);

    if (entry.body == nullptr || entry.body_len == 0) {
        shard->slots[slot_idx].body = nullptr;
        shard->slots[slot_idx].body_len = 0;
        return;
    }

    std::size_t copy_len = (entry.body_len > kL1MaxBodySize) ? kL1MaxBodySize : entry.body_len;
    std::memcpy(shard->bodies[slot_idx], entry.body, copy_len);
    if (copy_len < kL1MaxBodySize) {
        shard->bodies[slot_idx][copy_len] = '\0';
    }
    shard->slots[slot_idx].body = shard->bodies[slot_idx];
    shard->slots[slot_idx].body_len = copy_len;
}

void clear_slot(L1CacheShard* shard, std::size_t slot_idx) {
    shard->slots[slot_idx] = {};
    std::memset(shard->bodies[slot_idx], 0, kL1MaxBodySize);
    shard->generations[slot_idx] = 0;
    shard->key_hashes[slot_idx] = 0;
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

void l1_put(L1Cache* cache, const CacheEntry& entry, bytetaper::metrics::CacheMetrics* metrics) {
    if (cache == nullptr || entry.key == nullptr) {
        return;
    }
    if (entry.body_len > kL1MaxBodySize) {
        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::L1StoreRejectedBodyTooLarge);
        return;
    }
    if (entry.body_len > 0 && entry.body == nullptr) {
        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::L1StoreRejectedInvalidBody);
        return;
    }

    const std::uint64_t h = bytetaper::hash::hash_cstr_runtime(entry.key);
    const std::size_t shard_idx = h % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    std::scoped_lock lock(shard.mutex);

    std::size_t target_slot = shard.write_cursor % kL1SlotsPerShard;
    bool found = false;

    // Scan for duplicate key
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        if (shard.generations[i] > 0 && shard.key_hashes[i] == h &&
            std::strcmp(shard.slots[i].key, entry.key) == 0) {
            target_slot = i;
            found = true;
            bytetaper::metrics::record_cache_event(
                metrics, bytetaper::metrics::CacheMetricEvent::L1DuplicateOverwrite);
            break;
        }
    }

    if (!found) {
        if (shard.generations[target_slot] > 0) {
            bytetaper::metrics::record_cache_event(
                metrics, bytetaper::metrics::CacheMetricEvent::L1EvictionRingOverwrite);
        }
    }

    bytetaper::metrics::record_cache_event(metrics,
                                           bytetaper::metrics::CacheMetricEvent::L1StoreAdmitted);

    shard.slots[target_slot] = entry;
    copy_body_to_slot(&shard, target_slot, entry);
    shard.key_hashes[target_slot] = h;
    shard.generations[target_slot] += 1;

    if (!found) {
        shard.write_cursor += 1;
    }
}

bool l1_put_if_newer(L1Cache* cache, const CacheEntry& entry,
                     bytetaper::metrics::CacheMetrics* metrics) {
    if (cache == nullptr || entry.key == nullptr) {
        return false;
    }
    if (entry.body_len > kL1MaxBodySize) {
        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::L1StoreRejectedBodyTooLarge);
        return false;
    }
    if (entry.body_len > 0 && entry.body == nullptr) {
        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::L1StoreRejectedInvalidBody);
        return false;
    }

    const std::uint64_t h = bytetaper::hash::hash_cstr_runtime(entry.key);
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
            target_slot = i;
            found = true;
            bytetaper::metrics::record_cache_event(
                metrics, bytetaper::metrics::CacheMetricEvent::L1DuplicateOverwrite);
            break;
        }
    }

    if (!found) {
        if (shard.generations[target_slot] > 0) {
            bytetaper::metrics::record_cache_event(
                metrics, bytetaper::metrics::CacheMetricEvent::L1EvictionRingOverwrite);
        }
    }

    bytetaper::metrics::record_cache_event(metrics,
                                           bytetaper::metrics::CacheMetricEvent::L1StoreAdmitted);

    // 2. Perform insertion
    shard.slots[target_slot] = entry;
    copy_body_to_slot(&shard, target_slot, entry);
    shard.key_hashes[target_slot] = h;
    shard.generations[target_slot] += 1;

    // Only increment cursor if we didn't overwrite an existing slot.
    if (!found) {
        shard.write_cursor += 1;
    }
    return true;
}

bool l1_get(const L1Cache* cache, const char* key, std::int64_t now_ms, CacheEntry* out,
            char* body_out, std::size_t body_out_capacity,
            bytetaper::metrics::CacheMetrics* metrics) {
    if (cache == nullptr || key == nullptr || out == nullptr) {
        return false;
    }

    const std::uint64_t h = bytetaper::hash::hash_cstr_runtime(key);
    const std::size_t shard_idx = h % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    std::scoped_lock lock(shard.mutex);

    // Linear Scan within the shard.
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        if (shard.generations[i] == 0) {
            continue;
        }

        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::L1LookupSlotScanned);

        if (shard.key_hashes[i] != h) {
            continue;
        }
        if (std::strncmp(shard.slots[i].key, key, kCacheKeyMaxLen) != 0) {
            continue;
        }
        if (now_ms > 0 && is_expired(shard.slots[i], now_ms)) {
            bytetaper::metrics::record_cache_event(
                metrics, bytetaper::metrics::CacheMetricEvent::L1ExpiredMiss);
            continue;
        }

        *out = shard.slots[i];

        // Copy body into provided buffer while locked to prevent torn reads.
        if (out->body_len > 0 && body_out != nullptr && body_out_capacity > 0) {
            std::size_t copy_len =
                (out->body_len > body_out_capacity) ? body_out_capacity : out->body_len;
            std::memcpy(body_out, shard.bodies[i], copy_len);
            if (copy_len < body_out_capacity) {
                body_out[copy_len] = '\0';
            }
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

L1RemoveResult l1_remove(L1Cache* cache, const char* key,
                         bytetaper::metrics::CacheMetrics* metrics) {
    if (cache == nullptr || key == nullptr) {
        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::L1RemoveFailed);
        return L1RemoveResult::InvalidArgument;
    }

    const bool is_variant = (std::strncmp(key, "var:", 4) == 0);
    if (is_variant) {
        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::VariantExactRemoveAttempt);
    }

    const std::uint64_t h = bytetaper::hash::hash_cstr_runtime(key);
    const std::size_t shard_idx = h % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    std::scoped_lock lock(shard.mutex);

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

        clear_slot(&shard, i);
        bytetaper::metrics::record_cache_event(metrics,
                                               bytetaper::metrics::CacheMetricEvent::L1RemoveHit);
        if (is_variant) {
            bytetaper::metrics::record_cache_event(
                metrics, bytetaper::metrics::CacheMetricEvent::VariantExactRemoveSuccess);
        }
        return L1RemoveResult::Removed;
    }

    bytetaper::metrics::record_cache_event(metrics,
                                           bytetaper::metrics::CacheMetricEvent::L1RemoveMiss);
    if (is_variant) {
        bytetaper::metrics::record_cache_event(
            metrics, bytetaper::metrics::CacheMetricEvent::VariantExactRemoveMiss);
    }
    return L1RemoveResult::Miss;
}

bool l1_remove_key(L1Cache* cache, const char* key, bytetaper::metrics::CacheMetrics* metrics) {
    return l1_remove(cache, key, metrics) == L1RemoveResult::Removed;
}

} // namespace bytetaper::cache
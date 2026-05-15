// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/route_cache_epoch_store.h"

#include <cstring>

namespace bytetaper::runtime {

RouteCacheEpochResult route_cache_epoch_register(RouteCacheEpochStore* store,
                                                 const char* route_id) {
    if (store == nullptr || route_id == nullptr) {
        return RouteCacheEpochResult::InvalidArgument;
    }

    std::scoped_lock lock(store->mutex);

    // 1. Check for existing
    for (std::size_t i = 0; i < store->count; ++i) {
        if (std::strcmp(store->entries[i].route_id, route_id) == 0) {
            return RouteCacheEpochResult::Ok;
        }
    }

    // 2. Add new
    if (store->count >= kMaxRouteCacheEpochEntries) {
        return RouteCacheEpochResult::CapacityExceeded;
    }

    auto& entry = store->entries[store->count];
    std::strncpy(entry.route_id, route_id, sizeof(entry.route_id) - 1);
    entry.route_id[sizeof(entry.route_id) - 1] = '\0';
    entry.epoch.store(kInitialRouteCacheEpoch, std::memory_order_relaxed);

    store->count++;
    return RouteCacheEpochResult::Ok;
}

RouteCacheEpochResult route_cache_epoch_get(const RouteCacheEpochStore* store, const char* route_id,
                                            std::uint64_t* out_epoch) {
    if (store == nullptr || route_id == nullptr || out_epoch == nullptr) {
        return RouteCacheEpochResult::InvalidArgument;
    }

    std::scoped_lock lock(store->mutex);

    for (std::size_t i = 0; i < store->count; ++i) {
        if (std::strcmp(store->entries[i].route_id, route_id) == 0) {
            *out_epoch = store->entries[i].epoch.load(std::memory_order_relaxed);
            return RouteCacheEpochResult::Ok;
        }
    }

    return RouteCacheEpochResult::NotFound;
}

RouteCacheEpochResult route_cache_epoch_bump(RouteCacheEpochStore* store, const char* route_id,
                                             std::uint64_t* out_new_epoch) {
    if (store == nullptr || route_id == nullptr) {
        return RouteCacheEpochResult::InvalidArgument;
    }

    std::scoped_lock lock(store->mutex);

    for (std::size_t i = 0; i < store->count; ++i) {
        if (std::strcmp(store->entries[i].route_id, route_id) == 0) {
            std::uint64_t next =
                store->entries[i].epoch.fetch_add(1, std::memory_order_relaxed) + 1;
            if (out_new_epoch != nullptr) {
                *out_new_epoch = next;
            }
            return RouteCacheEpochResult::Ok;
        }
    }

    return RouteCacheEpochResult::NotFound;
}

RouteCacheEpochResult route_cache_epoch_reset_for_tests(RouteCacheEpochStore* store,
                                                        const char* route_id, std::uint64_t epoch) {
    if (store == nullptr || route_id == nullptr) {
        return RouteCacheEpochResult::InvalidArgument;
    }

    std::scoped_lock lock(store->mutex);

    for (std::size_t i = 0; i < store->count; ++i) {
        if (std::strcmp(store->entries[i].route_id, route_id) == 0) {
            store->entries[i].epoch.store(epoch, std::memory_order_relaxed);
            return RouteCacheEpochResult::Ok;
        }
    }

    return RouteCacheEpochResult::NotFound;
}

} // namespace bytetaper::runtime

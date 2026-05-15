// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_ROUTE_CACHE_EPOCH_STORE_H
#define BYTETAPER_RUNTIME_ROUTE_CACHE_EPOCH_STORE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace bytetaper::runtime {

static constexpr std::uint64_t kInitialRouteCacheEpoch = 1;
static constexpr std::size_t kMaxRouteCacheEpochEntries = 1024;

enum class RouteCacheEpochResult : std::uint8_t {
    Ok,
    NotFound,
    InvalidArgument,
    CapacityExceeded,
};

struct RouteCacheEpochEntry {
    char route_id[64] = {};
    std::atomic<std::uint64_t> epoch{ kInitialRouteCacheEpoch };
};

struct RouteCacheEpochStore {
    mutable std::mutex mutex;
    RouteCacheEpochEntry entries[kMaxRouteCacheEpochEntries];
    std::size_t count = 0;
};

// Register route (idempotent, starts at epoch 1)
RouteCacheEpochResult route_cache_epoch_register(RouteCacheEpochStore* store, const char* route_id);

// Read current epoch for route
RouteCacheEpochResult route_cache_epoch_get(const RouteCacheEpochStore* store, const char* route_id,
                                            std::uint64_t* out_epoch);

// Atomic increment — future mutation invalidation calls this
RouteCacheEpochResult route_cache_epoch_bump(RouteCacheEpochStore* store, const char* route_id,
                                             std::uint64_t* out_new_epoch);

// Test-only reset
RouteCacheEpochResult route_cache_epoch_reset_for_tests(RouteCacheEpochStore* store,
                                                        const char* route_id, std::uint64_t epoch);

} // namespace bytetaper::runtime

#endif // BYTETAPER_RUNTIME_ROUTE_CACHE_EPOCH_STORE_H

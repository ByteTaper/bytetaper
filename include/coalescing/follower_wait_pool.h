// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_COALESCING_FOLLOWER_WAIT_POOL_H
#define BYTETAPER_COALESCING_FOLLOWER_WAIT_POOL_H

#include "coalescing/inflight_registry.h"
#include "metrics/coalescing_metrics.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace bytetaper::coalescing {

static constexpr std::size_t kFollowerWaitPoolMaxWorkers = 4;
static constexpr std::size_t kFollowerWaitPoolQueueCapacity = 256;

struct FollowerWaitPoolConfig {
    std::size_t worker_count = 2;
    std::size_t queue_capacity = kFollowerWaitPoolQueueCapacity;
};

struct FollowerWaitPoolResources {
    coalescing::InFlightRegistry* registry = nullptr;
    metrics::CoalescingMetrics* metrics = nullptr;
};

struct FollowerWaitResultSlot {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    RegistryWaitResult result = RegistryWaitResult::Missing;
    RegistrySharedResponseOutput response{};
};

struct FollowerWaitJob {
    char key[256] = {};
    std::uint32_t wait_window_ms = 0;
    std::uint64_t lifecycle_generation = 0;
    FollowerWaitResultSlot* result_slot = nullptr;
};

struct FollowerWaitPool {
    FollowerWaitPoolConfig config;
    FollowerWaitPoolResources resources;

    // Internal job queue
    std::mutex queue_mutex;
    std::condition_variable queue_not_empty;
    std::condition_variable queue_not_full;

    std::vector<FollowerWaitJob> queue;
    std::size_t queue_head = 0;
    std::size_t queue_tail = 0;
    std::size_t queue_size = 0;

    std::vector<std::thread> workers;
    bool shutdown = false;
};

const char* follower_wait_pool_init(FollowerWaitPool* pool, const FollowerWaitPoolConfig& cfg);
const char* follower_wait_pool_start(FollowerWaitPool* pool, const FollowerWaitPoolResources& res);
void follower_wait_pool_shutdown(FollowerWaitPool* pool);

// Enqueues job to pool worker, then blocks on result_slot->cv until done.
// Returns false if queue is full (caller should fall back).
bool follower_wait_pool_submit_and_wait(FollowerWaitPool* pool, const char* key,
                                        std::uint32_t wait_window_ms,
                                        std::uint64_t lifecycle_generation,
                                        RegistrySharedResponseOutput* response_out,
                                        RegistryWaitResult* result_out);

} // namespace bytetaper::coalescing

#endif // BYTETAPER_COALESCING_FOLLOWER_WAIT_POOL_H

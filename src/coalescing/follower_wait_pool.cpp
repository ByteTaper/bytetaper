// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/follower_wait_pool.h"

#include <cstring>

namespace bytetaper::coalescing {

const char* follower_wait_pool_init(FollowerWaitPool* pool, const FollowerWaitPoolConfig& cfg) {
    if (pool == nullptr) {
        return "null_pool";
    }
    pool->config = cfg;
    if (pool->config.worker_count > kFollowerWaitPoolMaxWorkers) {
        pool->config.worker_count = kFollowerWaitPoolMaxWorkers;
    }
    if (pool->config.queue_capacity > kFollowerWaitPoolQueueCapacity) {
        pool->config.queue_capacity = kFollowerWaitPoolQueueCapacity;
    }
    pool->queue.resize(pool->config.queue_capacity);
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_size = 0;
    pool->shutdown = false;
    return nullptr;
}

const char* follower_wait_pool_start(FollowerWaitPool* pool, const FollowerWaitPoolResources& res) {
    if (pool == nullptr) {
        return "null_pool";
    }
    pool->resources = res;
    pool->shutdown = false;

    pool->workers.reserve(pool->config.worker_count);
    for (std::size_t i = 0; i < pool->config.worker_count; ++i) {
        pool->workers.emplace_back([pool]() {
            while (true) {
                FollowerWaitJob job;
                {
                    std::unique_lock<std::mutex> lock(pool->queue_mutex);
                    pool->queue_not_empty.wait(
                        lock, [pool]() { return pool->shutdown || pool->queue_size > 0; });

                    if (pool->shutdown && pool->queue_size == 0) {
                        return;
                    }

                    job = pool->queue[pool->queue_head];
                    pool->queue_head = (pool->queue_head + 1) % pool->config.queue_capacity;
                    pool->queue_size--;
                    pool->queue_not_full.notify_one();
                }

                if (job.result_slot != nullptr) {
                    RegistrySharedResponseOutput resp{};
                    RegistryWaitResult result = registry_wait_for_completion(
                        pool->resources.registry, job.key, job.wait_window_ms, &resp);

                    {
                        std::lock_guard<std::mutex> slot_lock(job.result_slot->mu);
                        job.result_slot->result = result;
                        job.result_slot->response = resp;
                        job.result_slot->done = true;
                    }
                    job.result_slot->cv.notify_all();
                }
            }
        });
    }
    return nullptr;
}

void follower_wait_pool_shutdown(FollowerWaitPool* pool) {
    if (pool == nullptr) {
        return;
    }

    std::vector<FollowerWaitResultSlot*> pending_slots;
    {
        std::lock_guard<std::mutex> lock(pool->queue_mutex);
        if (pool->shutdown) {
            return;
        }
        pool->shutdown = true;

        while (pool->queue_size > 0) {
            FollowerWaitJob& job = pool->queue[pool->queue_head];
            if (job.result_slot != nullptr) {
                pending_slots.push_back(job.result_slot);
            }
            pool->queue_head = (pool->queue_head + 1) % pool->config.queue_capacity;
            pool->queue_size--;
        }
    }
    pool->queue_not_empty.notify_all();
    pool->queue_not_full.notify_all();

    for (auto& worker : pool->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    pool->workers.clear();

    for (auto* slot : pending_slots) {
        std::lock_guard<std::mutex> slot_lock(slot->mu);
        slot->result = RegistryWaitResult::Missing;
        slot->done = true;
        slot->cv.notify_all();
    }
}

bool follower_wait_pool_submit_and_wait(FollowerWaitPool* pool, const char* key,
                                        std::uint32_t wait_window_ms,
                                        RegistrySharedResponseOutput* response_out,
                                        RegistryWaitResult* result_out) {
    if (pool == nullptr || key == nullptr) {
        return false;
    }

    FollowerWaitResultSlot slot{};
    {
        std::unique_lock<std::mutex> lock(pool->queue_mutex);
        if (pool->shutdown) {
            return false;
        }

        if (pool->queue_size >= pool->config.queue_capacity) {
            return false;
        }

        FollowerWaitJob job;
        std::strncpy(job.key, key, sizeof(job.key) - 1);
        job.key[sizeof(job.key) - 1] = '\0';
        job.wait_window_ms = wait_window_ms;
        job.result_slot = &slot;

        pool->queue[pool->queue_tail] = job;
        pool->queue_tail = (pool->queue_tail + 1) % pool->config.queue_capacity;
        pool->queue_size++;
        pool->queue_not_empty.notify_one();
    }

    std::unique_lock<std::mutex> slot_lock(slot.mu);
    slot.cv.wait(slot_lock, [&slot]() { return slot.done; });

    if (result_out != nullptr) {
        *result_out = slot.result;
    }
    if (response_out != nullptr && slot.result == RegistryWaitResult::SharedResponseReady) {
        *response_out = slot.response;
    }

    return true;
}

} // namespace bytetaper::coalescing

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime/worker_queue.h"

#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/inflight_registry.h"
#include "hash/hash.h"
#include "metrics/coalescing_metrics.h"
#include "metrics/runtime_metrics.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace bytetaper::runtime {

namespace {

static bool worker_ready_try_push(WorkerQueue* q, std::size_t worker_id, std::size_t shard_id);
static bool worker_ready_try_pop(WorkerQueue* q, std::size_t worker_id, std::size_t* shard_id_out);
static bool worker_ready_wait_pop(WorkerQueue* q, std::size_t worker_id, std::size_t* shard_id_out);
static bool shard_try_pop_one_job(WorkerQueue* q, std::size_t shard_idx,
                                  DequeuedRuntimeJob* job_out);
static void shard_requeue_or_clear(WorkerQueue* q, std::size_t shard_idx);

static WorkerEvent worker_wait_for_event(WorkerQueue* q, std::size_t worker_id);
static void process_ready_shard(WorkerQueue* q, std::size_t worker_id, std::size_t shard_id);
static void drain_owned_shards(WorkerQueue* q, std::size_t worker_id);

// Simple hash for selecting shard.
static std::uint32_t hash_key_to_shard(const char* key) {
    return static_cast<std::uint32_t>(bytetaper::hash::hash_cstr_runtime(key) % kRuntimeShardCount);
}

static std::size_t shard_owner(std::size_t shard_idx, std::size_t worker_count) {
    return shard_idx % worker_count;
}

static bool worker_has_no_remaining_work(WorkerQueue* q, std::size_t worker_id) {
    if (q == nullptr || worker_id >= q->worker_count) {
        return true;
    }
    const std::size_t owned_count = q->worker_owned_shard_count[worker_id];
    for (std::size_t i = 0; i < owned_count; ++i) {
        const std::size_t s_idx = q->worker_owned_shards[worker_id][i];
        RuntimeShard& shard = q->shards[s_idx];
        std::scoped_lock lock(shard.mu);
        if (shard.lookup_count > 0 || shard.store_count > 0) {
            return false;
        }
    }
    return true;
}

static bool shard_pending_try_mark(RuntimeShard* s, const char* key, std::uint32_t hash) {
    for (std::size_t i = 0; i < kRuntimePendingSlotsPerShard; i++) {
        if (s->pending_occupied[i] && s->pending_hashes[i] == hash &&
            std::strcmp(s->pending_keys[i], key) == 0) {
            return false; // duplicate
        }
    }
    for (std::size_t i = 0; i < kRuntimePendingSlotsPerShard; i++) {
        if (!s->pending_occupied[i]) {
            std::strncpy(s->pending_keys[i], key, cache::kCacheKeyMaxLen - 1);
            s->pending_hashes[i] = hash;
            s->pending_occupied[i] = true;
            s->pending_count++;
            return true;
        }
    }
    return false; // full
}

static void shard_pending_clear(RuntimeShard* s, const char* key, std::uint32_t hash) {
    for (std::size_t i = 0; i < kRuntimePendingSlotsPerShard; i++) {
        if (s->pending_occupied[i] && s->pending_hashes[i] == hash &&
            std::strcmp(s->pending_keys[i], key) == 0) {
            s->pending_occupied[i] = false;
            s->pending_hashes[i] = 0;
            s->pending_count--;
            return;
        }
    }
}

static void execute_lookup_job(WorkerQueue* q, RuntimeShard* shard, L2LookupJob& job,
                               char* scratch_buf, std::size_t scratch_len) {
    ::bytetaper::metrics::record_runtime_event(
        q->resources.runtime_metrics, ::bytetaper::metrics::RuntimeMetricEvent::JobExecuted);

    auto* l1 = q->resources.l1_cache;
    auto* l2 = q->resources.l2_cache;
    auto* m = q->resources.runtime_metrics;

    if (l2 != nullptr) {
        cache::CacheEntry hit{};
        const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();

        bool found = cache::l2_get(l2, job.key, now_ms, &hit, scratch_buf, scratch_len);
        if (found) {
            ::bytetaper::metrics::record_runtime_event(
                m, ::bytetaper::metrics::RuntimeMetricEvent::L2LookupHit);
            if (l1 != nullptr) {
                if (!cache::l1_can_store_entry(hit)) {
                    ::bytetaper::metrics::record_runtime_event(
                        m, ::bytetaper::metrics::RuntimeMetricEvent::
                               L2ToL1PromotionSkippedBodyTooLarge);
                } else if (cache::l1_put_if_newer(l1, hit)) {
                    ::bytetaper::metrics::record_runtime_event(
                        m, ::bytetaper::metrics::RuntimeMetricEvent::L2ToL1Promotion);
                } else {
                    ::bytetaper::metrics::record_runtime_event(
                        m, ::bytetaper::metrics::RuntimeMetricEvent::L2ToL1StaleRejected);
                }
            }
        } else {
            ::bytetaper::metrics::record_runtime_event(
                m, ::bytetaper::metrics::RuntimeMetricEvent::L2LookupMiss);
        }
    } else {
        ::bytetaper::metrics::record_runtime_event(
            m, ::bytetaper::metrics::RuntimeMetricEvent::L2LookupError);
        ::bytetaper::metrics::record_runtime_event(
            m, ::bytetaper::metrics::RuntimeMetricEvent::JobError);
    }

    {
        std::lock_guard<std::mutex> lock(shard->mu);
        shard_pending_clear(shard, job.key, job.key_hash);
    }
}

static void execute_store_job(WorkerQueue* q, RuntimeShard* shard, L2StoreJob& job) {
    ::bytetaper::metrics::record_runtime_event(
        q->resources.runtime_metrics, ::bytetaper::metrics::RuntimeMetricEvent::JobExecuted);

    auto* l2 = q->resources.l2_cache;
    auto* m = q->resources.runtime_metrics;
    auto* cm = q->resources.coalescing_metrics;

    const std::uint64_t now_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());

    if (l2 != nullptr) {
        if (cache::l2_put(l2, job.entry)) {
            ::bytetaper::metrics::record_runtime_event(
                m, ::bytetaper::metrics::RuntimeMetricEvent::L2StoreSuccess);
            if (job.coalescing_handoff_enabled && job.coalescing_registry != nullptr) {
                coalescing::registry_complete_state_if_generation(
                    job.coalescing_registry, job.coalescing_key, job.lifecycle_generation,
                    coalescing::InFlightCompletionState::L2Ready, now_ms);
                ::bytetaper::metrics::record_coalescing_event(
                    cm, ::bytetaper::metrics::CoalescingMetricEvent::LeaderL2HandoffReady);
            }
        } else {
            ::bytetaper::metrics::record_runtime_event(
                m, ::bytetaper::metrics::RuntimeMetricEvent::L2StoreError);
            ::bytetaper::metrics::record_runtime_event(
                m, ::bytetaper::metrics::RuntimeMetricEvent::JobError);
            if (job.coalescing_handoff_enabled && job.coalescing_registry != nullptr) {
                coalescing::registry_complete_state_if_generation(
                    job.coalescing_registry, job.coalescing_key, job.lifecycle_generation,
                    coalescing::InFlightCompletionState::Failed, now_ms);
                ::bytetaper::metrics::record_coalescing_event(
                    cm, ::bytetaper::metrics::CoalescingMetricEvent::LeaderL2HandoffFailed);
            }
        }
    } else {
        ::bytetaper::metrics::record_runtime_event(
            m, ::bytetaper::metrics::RuntimeMetricEvent::L2StoreError);
        ::bytetaper::metrics::record_runtime_event(
            m, ::bytetaper::metrics::RuntimeMetricEvent::JobError);
        if (job.coalescing_handoff_enabled && job.coalescing_registry != nullptr) {
            coalescing::registry_complete_state_if_generation(
                job.coalescing_registry, job.coalescing_key, job.lifecycle_generation,
                coalescing::InFlightCompletionState::Failed, now_ms);
            ::bytetaper::metrics::record_coalescing_event(
                cm, ::bytetaper::metrics::CoalescingMetricEvent::LeaderL2HandoffFailed);
        }
    }

    // Free body pool slot occupancy
    {
        std::lock_guard<std::mutex> lock(shard->mu);
        const std::uint32_t slot = job.body_slot;
        if (slot < kRuntimeQueueSlotsPerShard) {
            std::free(shard->body_pool.heap_bodies[slot]);
            shard->body_pool.heap_bodies[slot] = nullptr;
            shard->body_pool.heap_body_sizes[slot] = 0;
            shard->body_pool.occupied[slot] = false;
        }
    }
}

static WorkerEvent worker_wait_for_event(WorkerQueue* q, std::size_t worker_id) {
    std::size_t shard_id = 0;
    bool got_shard = worker_ready_wait_pop(q, worker_id, &shard_id);
    if (got_shard) {
        return WorkerEvent{ WorkerEventKind::ReadyShard, shard_id };
    }
    return WorkerEvent{ WorkerEventKind::Shutdown, 0 };
}

static void process_ready_shard(WorkerQueue* q, std::size_t worker_id, std::size_t shard_id) {
    DequeuedRuntimeJob job;
    if (shard_try_pop_one_job(q, shard_id, &job)) {
        if (job.kind == DequeuedJobKind::Lookup) {
            execute_lookup_job(q, &q->shards[shard_id], job.lookup_job,
                               q->worker_scratch[worker_id].l2_lookup_body, kAsyncL2MaxBodySize);
        } else if (job.kind == DequeuedJobKind::Store) {
            execute_store_job(q, &q->shards[shard_id], job.store_job);
        }
    }
    shard_requeue_or_clear(q, shard_id);
}

static void drain_owned_shards(WorkerQueue* q, std::size_t worker_id) {
    while (worker_drain_owned_once(q, worker_id)) {
        // Drain until no owned shards have work remaining
    }
}

// Worker loop invariants:
// 1. Worker sleeps when no owned shard has work.
// 2. Worker wakes when an owned shard is ready or shutdown is requested.
// 3. Worker only processes shards it owns.
// 4. Worker pops jobs under shard lock.
// 5. Worker releases shard lock before RocksDB I/O.
// 6. Worker clears pending lookup marker after L2 lookup completion.
// 7. Worker requeues shard if more jobs remain.
// 8. Worker drains owned shard jobs during shutdown according to current semantics.
// 9. Worker must not call backend.
// 10. Worker must not write to Envoy/gRPC stream.
static void worker_loop(WorkerQueue* q, std::size_t worker_id) {
    WorkerState state = WorkerState::Running;

    while (state != WorkerState::Stopped) {
        switch (state) {
        case WorkerState::Running: {
            const WorkerEvent ev = worker_wait_for_event(q, worker_id);
            if (ev.kind == WorkerEventKind::ReadyShard) {
                process_ready_shard(q, worker_id, ev.shard_id);
            } else {
                state = WorkerState::Draining;
            }
            break;
        }
        case WorkerState::Draining:
            drain_owned_shards(q, worker_id);
            state = WorkerState::Stopped;
            break;
        case WorkerState::Stopped:
            break;
        }
    }
}

[[maybe_unused]] static bool shard_try_pop_one_job(WorkerQueue* q, std::size_t shard_idx,
                                                   DequeuedRuntimeJob* job_out) {
    if (q == nullptr || shard_idx >= kRuntimeShardCount || job_out == nullptr) {
        return false;
    }
    RuntimeShard& shard = q->shards[shard_idx];
    std::lock_guard<std::mutex> lock(shard.mu);

    if (shard.lookup_count > 0) {
        job_out->kind = DequeuedJobKind::Lookup;
        job_out->lookup_job = shard.lookup_slots[shard.lookup_head];
        shard.lookup_head = (shard.lookup_head + 1) % kRuntimeQueueSlotsPerShard;
        shard.lookup_count--;

        if (q->resources.runtime_metrics != nullptr) {
            q->resources.runtime_metrics->worker_queue_depth.fetch_sub(1,
                                                                       std::memory_order_relaxed);
        }
        return true;
    }

    if (shard.store_count > 0) {
        job_out->kind = DequeuedJobKind::Store;
        job_out->store_job = shard.store_slots[shard.store_head];
        shard.store_head = (shard.store_head + 1) % kRuntimeQueueSlotsPerShard;
        shard.store_count--;

        if (q->resources.runtime_metrics != nullptr) {
            q->resources.runtime_metrics->worker_queue_depth.fetch_sub(1,
                                                                       std::memory_order_relaxed);
        }
        return true;
    }

    return false;
}

[[maybe_unused]] static void shard_requeue_or_clear(WorkerQueue* q, std::size_t shard_idx) {
    if (q == nullptr || shard_idx >= kRuntimeShardCount) {
        return;
    }
    RuntimeShard& shard = q->shards[shard_idx];
    bool should_requeue = false;
    {
        std::lock_guard<std::mutex> lock(shard.mu);
        if (shard.lookup_count > 0 || shard.store_count > 0) {
            shard.ready_enqueued = true;
            should_requeue = true;
        } else {
            shard.ready_enqueued = false;
        }
    }

    if (should_requeue) {
        std::size_t owner_w = shard_idx % q->worker_count;
        worker_ready_try_push(q, owner_w, shard_idx);
    }
}

// Primitives for managing the Ready Shard Queues.
// Lock-ordering convention: never hold RuntimeShard::mu while acquiring WorkerReadyQueue::mu.
[[maybe_unused]] static bool worker_ready_try_push(WorkerQueue* q, std::size_t worker_id,
                                                   std::size_t shard_id) {
    if (q == nullptr || worker_id >= q->worker_count) {
        return false;
    }
    auto& rq = q->worker_ready[worker_id];
    std::scoped_lock lock(rq.mu);
    if (rq.count >= kRuntimeShardCount) {
        return false; // full
    }
    rq.shard_ids[rq.tail] = shard_id;
    rq.tail = (rq.tail + 1) % kRuntimeShardCount;
    rq.count++;
    rq.cv.notify_one();
    return true;
}

[[maybe_unused]] static bool worker_ready_try_pop(WorkerQueue* q, std::size_t worker_id,
                                                  std::size_t* shard_id_out) {
    if (q == nullptr || worker_id >= q->worker_count || shard_id_out == nullptr) {
        return false;
    }
    auto& rq = q->worker_ready[worker_id];
    std::scoped_lock lock(rq.mu);
    if (rq.count == 0) {
        return false;
    }
    *shard_id_out = rq.shard_ids[rq.head];
    rq.head = (rq.head + 1) % kRuntimeShardCount;
    rq.count--;
    return true;
}

[[maybe_unused]] static bool worker_ready_wait_pop(WorkerQueue* q, std::size_t worker_id,
                                                   std::size_t* shard_id_out) {
    if (q == nullptr || worker_id >= q->worker_count || shard_id_out == nullptr) {
        return false;
    }
    auto& rq = q->worker_ready[worker_id];
    std::unique_lock<std::mutex> lock(rq.mu);
    rq.cv.wait(lock,
               [q, &rq] { return rq.count > 0 || !q->running.load(std::memory_order_acquire); });
    if (rq.count == 0) {
        return false; // running was set to false or queue shutdown
    }
    *shard_id_out = rq.shard_ids[rq.head];
    rq.head = (rq.head + 1) % kRuntimeShardCount;
    rq.count--;
    return true;
}

} // namespace

const char* worker_queue_init(WorkerQueue* q, const WorkerQueueConfig& config) {
    if (q == nullptr) {
        return "queue pointer is null";
    }

    if (config.worker_count == 0 || config.worker_count > kWorkerQueueMaxWorkers) {
        return "invalid worker_count";
    }

    q->worker_count = config.worker_count;
    q->running.store(false, std::memory_order_release);

    // Reset ownership arrays and worker ready queues
    for (std::size_t w = 0; w < kWorkerQueueMaxWorkers; ++w) {
        q->worker_owned_shard_count[w] = 0;
        for (std::size_t s = 0; s < kRuntimeMaxShardsPerWorker; ++s) {
            q->worker_owned_shards[w][s] = 0;
        }
        q->worker_ready[w].head = 0;
        q->worker_ready[w].tail = 0;
        q->worker_ready[w].count = 0;
        std::memset(q->worker_ready[w].shard_ids, 0, sizeof(q->worker_ready[w].shard_ids));
    }

    // Populate ownership arrays
    for (std::size_t s_idx = 0; s_idx < kRuntimeShardCount; ++s_idx) {
        std::size_t w_id = s_idx % config.worker_count;
        q->worker_owned_shards[w_id][q->worker_owned_shard_count[w_id]] = s_idx;
        q->worker_owned_shard_count[w_id]++;
    }

    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        q->shards[i].lookup_head = 0;
        q->shards[i].lookup_tail = 0;
        q->shards[i].lookup_count = 0;
        q->shards[i].store_head = 0;
        q->shards[i].store_tail = 0;
        q->shards[i].store_count = 0;
        q->shards[i].pending_count = 0;
        for (std::size_t j = 0; j < kRuntimePendingSlotsPerShard; j++) {
            q->shards[i].pending_occupied[j] = false;
        }
        for (std::size_t j = 0; j < kRuntimeQueueSlotsPerShard; j++) {
            if (q->shards[i].body_pool.heap_bodies[j] != nullptr) {
                std::free(q->shards[i].body_pool.heap_bodies[j]);
                q->shards[i].body_pool.heap_bodies[j] = nullptr;
                q->shards[i].body_pool.heap_body_sizes[j] = 0;
            }
            q->shards[i].body_pool.occupied[j] = false;
        }
        q->shards[i].ready_enqueued = false;
    }

    return nullptr;
}

const char* worker_queue_start(WorkerQueue* q, const WorkerQueueResources& res) {
    if (q == nullptr) {
        return "queue pointer is null";
    }

    if (q->running.load(std::memory_order_acquire)) {
        return "queue already running";
    }

    q->running.store(true, std::memory_order_release);
    q->resources = res;
    if (res.runtime_metrics != nullptr) {
        res.runtime_metrics->worker_queue_capacity.store(
            kRuntimeShardCount * kRuntimeQueueSlotsPerShard, std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < q->worker_count; ++i) {
        q->workers[i] = std::thread(worker_loop, q, i);
    }

    return nullptr;
}

bool worker_queue_try_enqueue_lookup(WorkerQueue* q, const L2LookupJob& job) {
    if (q == nullptr || !q->running.load(std::memory_order_acquire)) {
        return false;
    }

    std::uint64_t h_64 = bytetaper::hash::hash_cstr_runtime(job.key);
    std::uint32_t hash = static_cast<std::uint32_t>(h_64 ^ (h_64 >> 32));
    std::uint32_t shard_idx = h_64 % kRuntimeShardCount;
    RuntimeShard& shard = q->shards[shard_idx];

    bool should_push_ready = false;
    {
        std::lock_guard<std::mutex> lock(shard.mu);
        if (!shard_pending_try_mark(&shard, job.key, hash)) {
            return false; // Already pending or registry full
        }

        if (shard.lookup_count >= kRuntimeQueueSlotsPerShard) {
            shard_pending_clear(&shard, job.key, hash);
            ::bytetaper::metrics::record_runtime_event(
                q->resources.runtime_metrics,
                ::bytetaper::metrics::RuntimeMetricEvent::EnqueueDropped);
            return false;
        }

        ::bytetaper::metrics::record_runtime_event(
            q->resources.runtime_metrics, ::bytetaper::metrics::RuntimeMetricEvent::Enqueue);
        if (q->resources.runtime_metrics != nullptr) {
            q->resources.runtime_metrics->worker_queue_depth.fetch_add(1,
                                                                       std::memory_order_relaxed);
        }

        L2LookupJob mutable_job = job;
        mutable_job.key_hash = hash;
        shard.lookup_slots[shard.lookup_tail] = mutable_job;
        shard.lookup_tail = (shard.lookup_tail + 1) % kRuntimeQueueSlotsPerShard;
        shard.lookup_count++;

        if (!shard.ready_enqueued) {
            shard.ready_enqueued = true;
            should_push_ready = true;
        }
    }

    if (should_push_ready) {
        std::size_t owner_w = shard_idx % q->worker_count;
        worker_ready_try_push(q, owner_w, shard_idx);
    }

    return true;
}

bool worker_queue_try_enqueue_store(WorkerQueue* q, const L2StoreJob& job) {
    if (q == nullptr || !q->running.load(std::memory_order_acquire)) {
        return false;
    }

    std::uint32_t shard_idx = hash_key_to_shard(job.key);
    RuntimeShard& shard = q->shards[shard_idx];

    bool should_push_ready = false;
    {
        std::lock_guard<std::mutex> lock(shard.mu);
        if (shard.store_count >= kRuntimeQueueSlotsPerShard) {
            ::bytetaper::metrics::record_runtime_event(
                q->resources.runtime_metrics,
                ::bytetaper::metrics::RuntimeMetricEvent::EnqueueDropped);
            return false;
        }

        // Find available body pool slot
        int free_slot = -1;
        for (std::size_t i = 0; i < kRuntimeQueueSlotsPerShard; ++i) {
            if (!shard.body_pool.occupied[i]) {
                free_slot = static_cast<int>(i);
                break;
            }
        }

        if (free_slot == -1) {
            // Should never occur since store_count < capacity
            ::bytetaper::metrics::record_runtime_event(
                q->resources.runtime_metrics,
                ::bytetaper::metrics::RuntimeMetricEvent::EnqueueDropped);
            return false;
        }

        // Copy body into shard body pool slot on the heap
        char* buf = nullptr;
        std::size_t copy_len = 0;
        if (job.entry.body != nullptr && job.body_len > 0) {
            copy_len =
                (job.body_len > kAsyncL2StoreMaxBodySize) ? kAsyncL2StoreMaxBodySize : job.body_len;
            buf = static_cast<char*>(std::malloc(copy_len + 1));
            if (buf == nullptr) {
                // malloc failed — treat as queue-full / drop
                ::bytetaper::metrics::record_runtime_event(
                    q->resources.runtime_metrics,
                    ::bytetaper::metrics::RuntimeMetricEvent::EnqueueDropped);
                return false;
            }
            std::memcpy(buf, job.entry.body, copy_len);
            buf[copy_len] = '\0';
        }

        ::bytetaper::metrics::record_runtime_event(
            q->resources.runtime_metrics, ::bytetaper::metrics::RuntimeMetricEvent::Enqueue);
        if (q->resources.runtime_metrics != nullptr) {
            q->resources.runtime_metrics->worker_queue_depth.fetch_add(1,
                                                                       std::memory_order_relaxed);
        }

        shard.store_slots[shard.store_tail] = job;
        shard.store_slots[shard.store_tail].body_slot = static_cast<std::uint32_t>(free_slot);
        shard.body_pool.heap_bodies[free_slot] = buf;
        shard.body_pool.heap_body_sizes[free_slot] = copy_len;
        shard.store_slots[shard.store_tail].body_len = copy_len;
        shard.store_slots[shard.store_tail].entry.body_len = copy_len;
        shard.store_slots[shard.store_tail].entry.body = buf;
        shard.body_pool.occupied[free_slot] = true;

        shard.store_tail = (shard.store_tail + 1) % kRuntimeQueueSlotsPerShard;
        shard.store_count++;

        if (!shard.ready_enqueued) {
            shard.ready_enqueued = true;
            should_push_ready = true;
        }
    }

    if (should_push_ready) {
        std::size_t owner_w = shard_idx % q->worker_count;
        worker_ready_try_push(q, owner_w, shard_idx);
    }

    return true;
}

void worker_queue_shutdown(WorkerQueue* q) {
    if (q == nullptr) {
        return;
    }

    q->running.store(false, std::memory_order_release);

    for (std::size_t i = 0; i < q->worker_count; ++i) {
        auto& rq = q->worker_ready[i];
        {
            std::lock_guard<std::mutex> lock(rq.mu);
        }
        rq.cv.notify_all();
    }

    for (std::size_t i = 0; i < q->worker_count; ++i) {
        if (q->workers[i].joinable()) {
            q->workers[i].join();
        }
    }
}

bool worker_queue_execute_one_for_test(WorkerQueue* q) {
    if (q == nullptr) {
        return false;
    }

    for (std::size_t i = 0; i < kRuntimeShardCount; ++i) {
        RuntimeShard& shard = q->shards[i];
        std::unique_lock<std::mutex> lock(shard.mu);

        // Execute lookup job first
        if (shard.lookup_count > 0) {
            L2LookupJob job = shard.lookup_slots[shard.lookup_head];
            shard.lookup_head = (shard.lookup_head + 1) % kRuntimeQueueSlotsPerShard;
            shard.lookup_count--;

            if (q->resources.runtime_metrics != nullptr) {
                q->resources.runtime_metrics->worker_queue_depth.fetch_sub(
                    1, std::memory_order_relaxed);
            }

            lock.unlock(); // Release lock before executing I/O
            execute_lookup_job(q, &shard, job, q->worker_scratch[0].l2_lookup_body,
                               kAsyncL2LookupScratchSize);
            return true;
        }

        // Execute store job second
        if (shard.store_count > 0) {
            L2StoreJob job = shard.store_slots[shard.store_head];
            shard.store_head = (shard.store_head + 1) % kRuntimeQueueSlotsPerShard;
            shard.store_count--;

            if (q->resources.runtime_metrics != nullptr) {
                q->resources.runtime_metrics->worker_queue_depth.fetch_sub(
                    1, std::memory_order_relaxed);
            }

            lock.unlock(); // Release lock before executing I/O
            execute_store_job(q, &shard, job);
            return true;
        }
    }

    return false;
}

bool worker_queue_shard_try_pop_for_test(WorkerQueue* q, std::size_t shard_idx,
                                         DequeuedRuntimeJob* job_out) {
    return shard_try_pop_one_job(q, shard_idx, job_out);
}

void worker_queue_shard_requeue_or_clear_for_test(WorkerQueue* q, std::size_t shard_idx) {
    shard_requeue_or_clear(q, shard_idx);
}

bool worker_drain_owned_once(WorkerQueue* q, std::size_t worker_id) {
    if (q == nullptr || worker_id >= q->worker_count) {
        return false;
    }
    bool worked = false;
    const std::size_t owned_count = q->worker_owned_shard_count[worker_id];
    for (std::size_t i = 0; i < owned_count; ++i) {
        const std::size_t s_idx = q->worker_owned_shards[worker_id][i];
        DequeuedRuntimeJob job;
        if (shard_try_pop_one_job(q, s_idx, &job)) {
            worked = true;
            if (job.kind == DequeuedJobKind::Lookup) {
                execute_lookup_job(q, &q->shards[s_idx], job.lookup_job,
                                   q->worker_scratch[worker_id].l2_lookup_body,
                                   kAsyncL2MaxBodySize);
            } else if (job.kind == DequeuedJobKind::Store) {
                execute_store_job(q, &q->shards[s_idx], job.store_job);
            }
        }
    }
    return worked;
}

bool worker_test_run_one_event(WorkerQueue* q, std::size_t worker_id) {
    if (q == nullptr || worker_id >= q->worker_count) {
        return false;
    }
    std::size_t shard_id = 0;
    if (worker_ready_try_pop(q, worker_id, &shard_id)) {
        process_ready_shard(q, worker_id, shard_id);
        return true;
    }
    if (!q->running.load(std::memory_order_acquire)) {
        drain_owned_shards(q, worker_id);
        return true;
    }
    return false;
}

} // namespace bytetaper::runtime

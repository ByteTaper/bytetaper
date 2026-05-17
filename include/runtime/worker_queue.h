// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_WORKER_QUEUE_H
#define BYTETAPER_RUNTIME_WORKER_QUEUE_H

#include "cache/cache_entry.h"
#include "cache/cache_entry_codec.h"
#include "cache/l2_disk_cache.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace bytetaper::cache {
struct L1Cache;
}

namespace bytetaper::metrics {
struct RuntimeMetrics;
struct CoalescingMetrics;
struct CacheMetrics;
} // namespace bytetaper::metrics

namespace bytetaper::coalescing {
struct InFlightRegistry;
}

namespace bytetaper::runtime {

enum class WorkerEventKind : std::uint8_t {
    ReadyShard,
    Shutdown,
};

struct WorkerEvent {
    WorkerEventKind kind;
    std::size_t shard_id; // valid only when kind == ReadyShard
};

enum class WorkerState : std::uint8_t {
    Running,
    Draining,
    Stopped,
};

static constexpr std::size_t kAsyncL2MaxBodySize = 65536; // 64 KB

// Default max body accepted by the async L2 store path when policy has no explicit limit.
static constexpr std::size_t kAsyncL2StoreDefaultMaxBodySize = 65536;
static constexpr std::size_t kAsyncL2StoreAbsoluteMaxBodySize = cache::kL2MaxBodySize;

// Lookup scratch buffer used for L2 → L1 body promotion. 64 KiB is sufficient
// because L1 bodies are bounded by the same limit as coalescing snapshots.
static constexpr std::size_t kAsyncL2LookupScratchSize = 65536;

static constexpr std::size_t kAsyncL2StoreEncScratchSize =
    cache::kCacheEntryEncodedOverhead + kAsyncL2StoreAbsoluteMaxBodySize;

// Sharding configuration.
static constexpr std::size_t kRuntimeShardCount = 256;
static constexpr std::size_t kRuntimeQueueSlotsPerShard = 16; // 256 * 8 = 2048 total slots
static constexpr std::size_t kRuntimePendingSlotsPerShard = 16;
static constexpr std::size_t kWorkerQueueMaxWorkers = 8;
static constexpr std::size_t kRuntimeMaxShardsPerWorker = kRuntimeShardCount;

static constexpr std::size_t kAsyncL2StoreDefaultBodyPoolSlotSize =
    kAsyncL2StoreDefaultMaxBodySize + 1;

struct L2LookupJob {
    char key[cache::kCacheKeyMaxLen] = {};
    std::uint32_t key_hash = 0;
    std::uint64_t enqueued_at_ms = 0;
};

struct L2StoreJob {
    char key[cache::kCacheKeyMaxLen] = {};
    cache::CacheEntry entry = {};
    std::uint32_t body_slot = 0;
    std::size_t body_len = 0;
    // Coalescing L2 handoff — populated only for leader large-body responses
    bool coalescing_handoff_enabled = false;
    coalescing::InFlightRegistry* coalescing_registry = nullptr;
    char coalescing_key[256] = {}; // matches CoalescingDecision::key capacity
    std::uint64_t lifecycle_generation = 0;
    std::uint64_t enqueued_at_ms = 0;
};

struct L2InvalidateJob {
    char key[cache::kCacheKeyMaxLen] = {};
    std::uint32_t key_hash = 0;
    std::uint64_t enqueued_at_ms = 0;
};

struct StoreBodyPool {
    char* slab = nullptr;
    char* bodies[kRuntimeQueueSlotsPerShard] = {};
    std::size_t body_sizes[kRuntimeQueueSlotsPerShard] = {};
    bool occupied[kRuntimeQueueSlotsPerShard] = {};
};

struct WorkerScratch {
    char l2_lookup_body[kAsyncL2LookupScratchSize] = {};
    char l2_store_enc_buf[kAsyncL2StoreEncScratchSize] = {};
};

struct WorkerReadyQueue {
    std::mutex mu;
    std::condition_variable cv;
    std::size_t shard_ids[kRuntimeShardCount] = {};
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t count = 0;
};

static constexpr std::size_t kRuntimeShardBatchQuota = 1;

struct WorkerQueueConfig {
    std::size_t worker_count = 2; // >= 1, <= kWorkerQueueMaxWorkers
    // 0 means use kAsyncL2StoreDefaultMaxBodySize. Server startup sets this from route
    // max_response_bytes when configured.
    std::size_t async_store_max_body_size = 0;
    std::size_t lookup_lane_quota = 4;     // >= 1
    std::size_t store_lane_quota = 1;      // >= 1
    std::size_t invalidate_lane_quota = 1; // >= 1
};

struct WorkerQueueResources {
    cache::L2DiskCache* l2_cache = nullptr;
    cache::L1Cache* l1_cache = nullptr;
    metrics::RuntimeMetrics* runtime_metrics = nullptr;
    metrics::CoalescingMetrics* coalescing_metrics = nullptr;
    metrics::CacheMetrics* cache_metrics = nullptr;
};

enum class RuntimeShardState : std::uint8_t {
    Idle,
    Queued,
    Processing,
};

// Represents a single sharded queue with its own lock and inline pending registry.
// Each worker owns multiple shards based on its index.
struct RuntimeShard {
    alignas(64) std::mutex mu;

    // Inline pending lookup dedup — shard-local, no external mutex.
    char pending_keys[kRuntimePendingSlotsPerShard][cache::kCacheKeyMaxLen] = {};
    std::uint32_t pending_hashes[kRuntimePendingSlotsPerShard] = {};
    bool pending_occupied[kRuntimePendingSlotsPerShard] = {};
    std::size_t pending_count = 0;

    // Lookup ring.
    L2LookupJob lookup_slots[kRuntimeQueueSlotsPerShard] = {};
    std::size_t lookup_head = 0;
    std::size_t lookup_tail = 0;
    std::size_t lookup_count = 0;

    // Store ring.
    L2StoreJob store_slots[kRuntimeQueueSlotsPerShard] = {};
    std::size_t store_head = 0;
    std::size_t store_tail = 0;
    std::size_t store_count = 0;

    // Invalidate ring.
    L2InvalidateJob invalidate_slots[kRuntimeQueueSlotsPerShard] = {};
    std::size_t invalidate_head = 0;
    std::size_t invalidate_tail = 0;
    std::size_t invalidate_count = 0;

    // Shard-local store body pool.
    StoreBodyPool body_pool = {};

    RuntimeShardState state = RuntimeShardState::Idle;
};

// Fixed-capacity worker queue with sharding. Must not be copied or moved after init.
struct WorkerQueue {
    RuntimeShard shards[kRuntimeShardCount];
    std::atomic<bool> running{ false };
    std::thread workers[kWorkerQueueMaxWorkers];
    std::size_t worker_count = 0;
    std::size_t worker_owned_shards[kWorkerQueueMaxWorkers][kRuntimeMaxShardsPerWorker] = {};
    std::size_t worker_owned_shard_count[kWorkerQueueMaxWorkers] = {};
    WorkerScratch worker_scratch[kWorkerQueueMaxWorkers];
    WorkerReadyQueue worker_ready[kWorkerQueueMaxWorkers];
    WorkerQueueResources resources{};
    std::atomic<std::size_t> store_body_pool_bytes_in_use{ 0 };
    std::size_t async_store_max_body_size = kAsyncL2StoreDefaultMaxBodySize;
    std::size_t async_store_body_pool_slot_size = kAsyncL2StoreDefaultBodyPoolSlotSize;
    std::size_t effective_worker_count = 0;
    std::size_t effective_lookup_lane_quota = 4;
    std::size_t effective_store_lane_quota = 1;
    std::size_t effective_invalidate_lane_quota = 1;
    std::size_t effective_async_store_max_body_size = 0;
};

// Validates config and initialises queue fields. Does not start threads.
// Returns nullptr on success, error string on failure.
const char* worker_queue_init(WorkerQueue* q, const WorkerQueueConfig& config);

// Starts the background worker threads. Returns nullptr on success, or error string.
// Must be called after worker_queue_init.
const char* worker_queue_start(WorkerQueue* q, const WorkerQueueResources& res);

// Non-blocking enqueue for lookup jobs.
bool worker_queue_try_enqueue_lookup(WorkerQueue* q, const L2LookupJob& job);

// Non-blocking enqueue for store jobs.
bool worker_queue_try_enqueue_store(WorkerQueue* q, const L2StoreJob& job);

// Non-blocking enqueue for L2 invalidate jobs.
bool worker_queue_enqueue_l2_invalidate(WorkerQueue* q, const char* key, std::uint64_t now_ms);

// Signals workers to stop and joins all threads.
// Caller must keep any L2DiskCache alive until this returns.
void worker_queue_shutdown(WorkerQueue* q);

/**
 * Dequeues and synchronously executes one job using q->resources.
 * Intended for deterministic unit tests only — does not start threads.
 * Returns true if a job was dequeued and executed; false if queue was empty.
 */
bool worker_queue_execute_one_for_test(WorkerQueue* q);

/**
 * Test helpers to verify internal pop and requeue/clear.
 */
bool worker_queue_shard_try_pop_lookup_for_test(WorkerQueue* q, std::size_t shard_idx,
                                                L2LookupJob* job_out);
bool worker_queue_shard_try_pop_store_for_test(WorkerQueue* q, std::size_t shard_idx,
                                               L2StoreJob* job_out);
void worker_queue_shard_requeue_or_clear_for_test(WorkerQueue* q, std::size_t shard_idx);

/**
 * Drain all owned shards once without waiting for events.
 */
bool worker_drain_owned_once(WorkerQueue* q, std::size_t worker_id);

/**
 * Process exactly one event for the given worker (non-blocking if queue is empty).
 */
bool worker_test_run_one_event(WorkerQueue* q, std::size_t worker_id);

RuntimeShardState worker_queue_shard_state_for_test(WorkerQueue* q, std::size_t shard_idx);
void worker_queue_shard_set_state_for_test(WorkerQueue* q, std::size_t shard_idx,
                                           RuntimeShardState state);
bool worker_queue_shard_try_mark_processing_for_test(WorkerQueue* q, std::size_t worker_id,
                                                     std::size_t shard_id);

} // namespace bytetaper::runtime

#endif // BYTETAPER_RUNTIME_WORKER_QUEUE_H

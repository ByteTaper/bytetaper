// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_TQ_CACHE_NAMESPACE_VERSIONING_H
#define BYTETAPER_TAPERQUERY_TQ_CACHE_NAMESPACE_VERSIONING_H

#include "runtime/route_cache_epoch_store.h"
#include "taperquery/tq_plan.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bytetaper::cache {
struct L1Cache;
struct L2DiskCache;
} // namespace bytetaper::cache

namespace bytetaper::metrics {
struct CacheMetrics;
}

namespace bytetaper::taperquery {

enum class TqCacheNamespaceImpact : std::uint8_t {
    None,
    RouteEpochBumpRequired,
};

struct TqRouteCacheNamespaceChange {
    std::string route_id;
    std::string before_route_identity;
    std::string after_route_identity;
    bool epoch_bump_required = false;
    bool would_cleanup_l1 = false;
    bool would_cleanup_l2 = false;
    std::uint64_t before_epoch = 0;
    std::uint64_t after_epoch = 0;
    std::vector<std::string> reasons; // e.g. "FieldFilteringBehavior"
};

struct TqCacheNamespaceVersioningResult {
    bool ok = false;
    std::vector<TqRouteCacheNamespaceChange> changed_routes;
    std::string error;
};

// Durable physical cleanup job abstraction matching phase specifications
struct RouteCacheCleanupJob {
    std::string route_id;
    std::uint64_t old_epoch = 0;
    std::uint64_t new_epoch = 0;
    std::string before_policy_identity;
    std::string after_policy_identity;
};

// Abstract cleanup queue interface for test double substitution
class RouteCacheCleanupQueue {
public:
    virtual ~RouteCacheCleanupQueue() = default;
    virtual void enqueue(const RouteCacheCleanupJob& job) = 0;
};

// Durable, thread-safe production-grade asynchronous cleanup queue implementation
class RouteCacheCleanupQueueImpl : public RouteCacheCleanupQueue {
public:
    explicit RouteCacheCleanupQueueImpl(cache::L2DiskCache* l2_cache = nullptr,
                                        metrics::CacheMetrics* metrics = nullptr);
    ~RouteCacheCleanupQueueImpl() override {
        shutdown();
    }

    void enqueue(const RouteCacheCleanupJob& job) override {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (shutdown_) {
                return;
            }
            jobs_.push_back(job);
        }
        cv_.notify_one();
    }

    void start_worker() {
        worker_thread_ = std::thread([this]() { worker_loop(); });
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (shutdown_) {
                return;
            }
            shutdown_ = true;
        }
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    std::vector<RouteCacheCleanupJob> get_completed_jobs() {
        std::lock_guard<std::mutex> lock(mu_);
        return completed_jobs_;
    }

    std::size_t get_pending_count() {
        std::lock_guard<std::mutex> lock(mu_);
        return jobs_.size();
    }

private:
    std::optional<RouteCacheCleanupJob> try_dequeue() {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this]() { return !jobs_.empty() || shutdown_; });
        if (shutdown_ && jobs_.empty())
            return std::nullopt;
        auto job = std::move(jobs_.front());
        jobs_.erase(jobs_.begin());
        return job;
    }

    void worker_loop() {
        while (auto job = try_dequeue()) {
            execute_cleanup(*job);
            {
                std::lock_guard<std::mutex> lock(mu_);
                completed_jobs_.push_back(*job);
            }
        }
    }

    void execute_cleanup(const RouteCacheCleanupJob& job);

    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<RouteCacheCleanupJob> jobs_;
    std::vector<RouteCacheCleanupJob> completed_jobs_;
    std::thread worker_thread_;
    bool shutdown_ = false;

    cache::L2DiskCache* l2_cache_ = nullptr;
    metrics::CacheMetrics* metrics_ = nullptr;
};

// Unified cleanup plan and results types
struct TqRouteCacheCleanupPlan {
    std::string route_id;
    std::uint64_t old_epoch = 0;
    std::uint64_t new_epoch = 0;
    std::string before_route_identity;
    std::string after_route_identity;
    bool l1_cleanup_required = false;
    bool l2_cleanup_required = false;
    bool variant_cleanup_required = false;
    std::vector<std::string> reasons;
    // Filled in after execution:
    std::size_t l1_removed_count = 0;
    bool l2_cleanup_enqueued = false;
    std::vector<std::string> warnings;
};

struct TqCacheNamespaceApplyResult {
    bool ok = false;
    std::vector<TqRouteCacheCleanupPlan> routes;
    std::string error;
};

// Detect affected routes from plan and bump their epochs.
// Returns !ok if a required bump fails.
TqCacheNamespaceVersioningResult
version_cache_namespace_for_apply_plan(const TqApplyPlan& plan,
                                       runtime::RouteCacheEpochStore* epoch_store);

// Detect only — no bump. Used by dry-run.
TqCacheNamespaceVersioningResult
detect_cache_namespace_impacts(const TqApplyPlan& plan, runtime::RouteCacheEpochStore* epoch_store);

// Unified function doing both epoch bump, synchronous L1 cleanup and async L2 enqueuing
TqCacheNamespaceApplyResult version_and_cleanup_cache_namespaces_for_apply(
    const TqApplyPlan& plan, runtime::RouteCacheEpochStore* epoch_store, cache::L1Cache* l1_cache,
    RouteCacheCleanupQueue* l2_cleanup_queue);

// Decoupled epoch bump/register stage (to be run BEFORE active policy snapshot swap)
TqCacheNamespaceApplyResult version_epochs_for_apply(const TqApplyPlan& plan,
                                                     runtime::RouteCacheEpochStore* epoch_store);

TqCacheNamespaceApplyResult
enqueue_route_cleanups_for_apply(const TqCacheNamespaceApplyResult& epoch_res,
                                 RouteCacheCleanupQueue* l2_cleanup_queue);

TqCacheNamespaceApplyResult
perform_l1_cleanups_for_apply(const TqCacheNamespaceApplyResult& epoch_res,
                              cache::L1Cache* l1_cache);

TqCacheNamespaceApplyResult
prepare_operational_sync_for_apply(const TqCacheNamespaceApplyResult& epoch_res,
                                   cache::L1Cache* l1_cache,
                                   RouteCacheCleanupQueue* l2_cleanup_queue);

// Decoupled cleanup stage (to be run AFTER successful active policy snapshot swap)
TqCacheNamespaceApplyResult
cleanup_cache_namespaces_for_apply(const TqCacheNamespaceApplyResult& epoch_res,
                                   cache::L1Cache* l1_cache,
                                   RouteCacheCleanupQueue* l2_cleanup_queue);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_TQ_CACHE_NAMESPACE_VERSIONING_H

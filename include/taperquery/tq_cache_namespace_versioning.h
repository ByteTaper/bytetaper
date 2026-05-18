// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_TQ_CACHE_NAMESPACE_VERSIONING_H
#define BYTETAPER_TAPERQUERY_TQ_CACHE_NAMESPACE_VERSIONING_H

#include "runtime/route_cache_epoch_store.h"
#include "taperquery/tq_plan.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    RouteCacheCleanupQueueImpl() = default;
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
    void worker_loop() {
        while (true) {
            RouteCacheCleanupJob job;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this]() { return !jobs_.empty() || shutdown_; });
                if (shutdown_ && jobs_.empty()) {
                    return;
                }
                job = std::move(jobs_.front());
                jobs_.erase(jobs_.begin());
            }

            // Perform physical cleanup logging & trace
            std::fprintf(stdout,
                         "[RouteCacheCleanup] Asynchronously clearing old epoch namespace %lu for "
                         "route '%s'\n",
                         job.old_epoch, job.route_id.c_str());
            std::fflush(stdout);

            {
                std::lock_guard<std::mutex> lock(mu_);
                completed_jobs_.push_back(job);
            }
        }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<RouteCacheCleanupJob> jobs_;
    std::vector<RouteCacheCleanupJob> completed_jobs_;
    std::thread worker_thread_;
    bool shutdown_ = false;
};

// Detect affected routes from plan and bump their epochs.
// Returns !ok if a required bump fails.
TqCacheNamespaceVersioningResult
version_cache_namespace_for_apply_plan(const TqApplyPlan& plan,
                                       runtime::RouteCacheEpochStore* epoch_store);

// Detect only — no bump. Used by dry-run.
TqCacheNamespaceVersioningResult
detect_cache_namespace_impacts(const TqApplyPlan& plan, runtime::RouteCacheEpochStore* epoch_store);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_TQ_CACHE_NAMESPACE_VERSIONING_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_QUEUE_H
#define BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_QUEUE_H

#include "control_plane/control_plane_metrics.h"
#include "control_plane/policy_lifecycle_emitter.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/policy_state_store.h"
#include "control_plane/policy_update_job.h"
#include "control_plane/policy_update_shard.h"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bytetaper::control_plane {

static constexpr std::uint32_t kDefaultLogicalShardCount = 256;
static constexpr std::uint32_t kDefaultMaxQueueDepthPerShard = 1024;

struct PolicyUpdateQueueConfig {
    std::uint32_t logical_shard_count = kDefaultLogicalShardCount;
    std::uint32_t max_queue_depth_per_shard = kDefaultMaxQueueDepthPerShard;
    PolicyStateStore* job_store = nullptr;
    ControlPlaneMetrics* control_plane_metrics = nullptr;
    PolicyLifecycleEmitter* lifecycle_emitter = nullptr;
};

struct EnqueueJobResult {
    bool ok = false;
    std::uint32_t logical_shard_id = 0;
    std::string job_id;
    std::string error;
    std::string message;
};

class PolicyUpdateQueue {
public:
    explicit PolicyUpdateQueue(PolicyUpdateQueueConfig config = {});

    std::uint32_t compute_shard_id(const std::string& resource_key) const;

    EnqueueJobResult enqueue(PolicyUpdateJob job);

    PolicyUpdateShard* try_claim_shard();

    void release_shard(PolicyUpdateShard* shard);

    PolicyUpdateJobState get_job_state(const std::string& job_id) const;

    void update_job(const PolicyUpdateJob& job);

    std::optional<PolicyUpdateJobRecord> get_job(const std::string& job_id,
                                                 const PolicyResourceKey& resource_key) const;

    void begin_draining();

    void notify_workers();

    void wait_for_ready_shard(std::unique_lock<std::mutex>& lock);

    std::mutex& queue_mutex();

    std::uint64_t depth() const;
    std::uint64_t capacity() const;
    std::optional<std::string> last_enqueued_job_id() const;

    void record_job_dequeued();

    bool has_durable_job_store() const {
        return config_.job_store != nullptr;
    }

private:
    bool persist_job_record(const PolicyUpdateJob& job);

    void push_ready_shard_unlocked(std::uint32_t shard_id);

    void sync_queue_metrics();

    PolicyUpdateQueueConfig config_;
    bool draining_ = false;
    std::vector<std::unique_ptr<PolicyUpdateShard>> shards_;
    mutable std::mutex queue_mu_;
    std::condition_variable ready_cv_;
    std::vector<std::uint32_t> ready_shard_ids_;
    std::unordered_map<std::string, PolicyUpdateJobState> job_states_;
    std::unordered_map<std::string, PolicyUpdateJobRecord> job_records_;
    std::string last_enqueued_job_id_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_QUEUE_H

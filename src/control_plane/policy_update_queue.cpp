// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_update_queue.h"

#include "control_plane/control_plane_metrics.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_update_job_record.h"

#include <algorithm>
#include <chrono>

namespace bytetaper::control_plane {

namespace {

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

PolicyUpdateQueue::PolicyUpdateQueue(PolicyUpdateQueueConfig config) : config_(config) {
    if (config_.logical_shard_count == 0) {
        config_.logical_shard_count = kDefaultLogicalShardCount;
    }
    shards_.reserve(config_.logical_shard_count);
    for (std::uint32_t i = 0; i < config_.logical_shard_count; ++i) {
        auto shard = std::make_unique<PolicyUpdateShard>();
        shard->shard_id = i;
        shard->state = PolicyUpdateShardState::Idle;
        shards_.push_back(std::move(shard));
    }
}

std::uint32_t PolicyUpdateQueue::compute_shard_id(const std::string& resource_key) const {
    const std::size_t hash = std::hash<std::string>{}(resource_key);
    return static_cast<std::uint32_t>(hash % config_.logical_shard_count);
}

bool PolicyUpdateQueue::persist_job_record(const PolicyUpdateJob& job) {
    if (config_.job_store == nullptr) {
        return false;
    }

    PolicyResourceKey key;
    if (!parse_resource_key(job.resource_key, &key)) {
        return false;
    }

    const PolicyUpdateJobRecord record = to_job_record(job);
    const StorePolicyUpdateJobResult store_res =
        config_.job_store->store_policy_update_job(key, record);
    return store_res.ok;
}

void PolicyUpdateQueue::push_ready_shard_unlocked(const std::uint32_t shard_id) {
    if (std::find(ready_shard_ids_.begin(), ready_shard_ids_.end(), shard_id) ==
        ready_shard_ids_.end()) {
        ready_shard_ids_.push_back(shard_id);
    }
}

void PolicyUpdateQueue::begin_draining() {
    std::lock_guard<std::mutex> queue_lock(queue_mu_);
    draining_ = true;

    for (std::uint32_t shard_id = 0; shard_id < shards_.size(); ++shard_id) {
        PolicyUpdateShard& shard = *shards_[shard_id];
        std::lock_guard<std::mutex> shard_lock(shard.mu);
        if (shard.jobs.empty()) {
            continue;
        }
        if (shard.state == PolicyUpdateShardState::Queued ||
            shard.state == PolicyUpdateShardState::Processing) {
            shard.state = PolicyUpdateShardState::Draining;
        }
        if (shard.state == PolicyUpdateShardState::Draining) {
            push_ready_shard_unlocked(shard_id);
        }
    }

    ready_cv_.notify_all();
}

EnqueueJobResult PolicyUpdateQueue::enqueue(PolicyUpdateJob job) {
    EnqueueJobResult result{};
    if (job.job_id.empty()) {
        result.error = "POLICY_JOB_ID_MISSING";
        return result;
    }
    if (job.resource_key.empty()) {
        result.error = "POLICY_JOB_RESOURCE_KEY_MISSING";
        return result;
    }

    const std::uint32_t shard_id = compute_shard_id(job.resource_key);
    PolicyUpdateShard& shard = *shards_[shard_id];
    const std::string job_id = job.job_id;

    {
        std::lock_guard<std::mutex> queue_lock(queue_mu_);
        std::lock_guard<std::mutex> shard_lock(shard.mu);
        result.logical_shard_id = shard_id;

        if (shard.state == PolicyUpdateShardState::Stopped) {
            result.error = "POLICY_JOB_QUEUE_STOPPED";
            return result;
        }
        if (draining_ || shard.state == PolicyUpdateShardState::Draining) {
            result.error = "POLICY_JOB_QUEUE_DRAINING";
            result.message = "Policy update queue is draining and not accepting new jobs.";
            return result;
        }
        if (shard.jobs.size() >= config_.max_queue_depth_per_shard) {
            result.error = "POLICY_JOB_QUEUE_FULL";
            result.message = "Policy update queue for this resource is full.";
            record_queue_rejection(config_.control_plane_metrics);
            return result;
        }

        job.logical_shard_id = shard_id;
        job.state = PolicyUpdateJobState::Queued;
        job.submitted_at_unix_epoch_ms = now_ms();
        job.updated_at_unix_epoch_ms = job.submitted_at_unix_epoch_ms;

        if (!persist_job_record(job)) {
            result.error = "POLICY_JOB_STORE_FAILED";
            return result;
        }

        const PolicyUpdateJobRecord queued_record = to_job_record(job);
        job_records_[job_id] = queued_record;
        job_states_[job_id] = PolicyUpdateJobState::Queued;

        shard.jobs.push_back(std::move(job));

        const bool was_idle = shard.state == PolicyUpdateShardState::Idle;
        shard.state = PolicyUpdateShardState::Queued;

        if (was_idle) {
            ready_shard_ids_.push_back(shard_id);
        }

        ready_cv_.notify_all();
        last_enqueued_job_id_ = job_id;
    }

    result.ok = true;
    result.logical_shard_id = shard_id;
    result.job_id = job_id;

    sync_queue_metrics();

    return result;
}

void PolicyUpdateQueue::sync_queue_metrics() {
    if (config_.control_plane_metrics == nullptr) {
        return;
    }
    config_.control_plane_metrics->policy_update_queue_depth.store(depth(),
                                                                   std::memory_order_relaxed);
    config_.control_plane_metrics->policy_update_queue_capacity.store(capacity(),
                                                                      std::memory_order_relaxed);
}

void PolicyUpdateQueue::record_job_dequeued() {
    sync_queue_metrics();
}

PolicyUpdateShard* PolicyUpdateQueue::try_claim_shard() {
    std::lock_guard<std::mutex> queue_lock(queue_mu_);
    if (ready_shard_ids_.empty()) {
        return nullptr;
    }

    for (auto it = ready_shard_ids_.begin(); it != ready_shard_ids_.end();) {
        const std::uint32_t shard_id = *it;
        PolicyUpdateShard& shard = *shards_[shard_id];
        std::lock_guard<std::mutex> shard_lock(shard.mu);
        const bool claimable = shard.state == PolicyUpdateShardState::Queued ||
                               shard.state == PolicyUpdateShardState::Draining;
        if (!claimable || shard.jobs.empty()) {
            it = ready_shard_ids_.erase(it);
            if (shard.jobs.empty() && (shard.state == PolicyUpdateShardState::Queued ||
                                       shard.state == PolicyUpdateShardState::Draining)) {
                shard.state = PolicyUpdateShardState::Idle;
            }
            continue;
        }

        shard.state = PolicyUpdateShardState::Processing;
        ready_shard_ids_.erase(it);
        return &shard;
    }
    return nullptr;
}

void PolicyUpdateQueue::release_shard(PolicyUpdateShard* shard) {
    if (shard == nullptr) {
        return;
    }

    {
        std::lock_guard<std::mutex> queue_lock(queue_mu_);
        std::lock_guard<std::mutex> shard_lock(shard->mu);
        if (shard->jobs.empty()) {
            shard->state = PolicyUpdateShardState::Idle;
        } else if (draining_) {
            shard->state = PolicyUpdateShardState::Draining;
            push_ready_shard_unlocked(shard->shard_id);
            ready_cv_.notify_all();
        } else {
            shard->state = PolicyUpdateShardState::Queued;
            push_ready_shard_unlocked(shard->shard_id);
            ready_cv_.notify_all();
        }
    }
}

PolicyUpdateJobState PolicyUpdateQueue::get_job_state(const std::string& job_id) const {
    std::lock_guard<std::mutex> lock(queue_mu_);
    const auto it = job_states_.find(job_id);
    if (it == job_states_.end()) {
        return PolicyUpdateJobState::Submitted;
    }
    return it->second;
}

void PolicyUpdateQueue::update_job(const PolicyUpdateJob& job) {
    const PolicyUpdateJobRecord record = to_job_record(job);
    {
        std::lock_guard<std::mutex> lock(queue_mu_);
        job_states_[job.job_id] = job.state;
        job_records_[job.job_id] = record;
    }
    persist_job_record(job);
}

std::optional<PolicyUpdateJobRecord>
PolicyUpdateQueue::get_job(const std::string& job_id, const PolicyResourceKey& resource_key) const {
    if (config_.job_store != nullptr) {
        const LoadPolicyUpdateJobResult load_res =
            config_.job_store->load_policy_update_job(resource_key, job_id);
        if (load_res.ok) {
            return load_res.record;
        }
        if (!load_res.not_found) {
            return std::nullopt;
        }
    }

    std::lock_guard<std::mutex> lock(queue_mu_);
    const auto it = job_records_.find(job_id);
    if (it != job_records_.end()) {
        return it->second;
    }

    const auto state_it = job_states_.find(job_id);
    if (state_it != job_states_.end()) {
        PolicyUpdateJobRecord record;
        record.job_id = job_id;
        record.resource_key = resource_key.to_string();
        record.state = to_string(state_it->second);
        return record;
    }

    return std::nullopt;
}

void PolicyUpdateQueue::notify_workers() {
    ready_cv_.notify_all();
}

void PolicyUpdateQueue::wait_for_ready_shard(std::unique_lock<std::mutex>& lock) {
    ready_cv_.wait_for(lock, std::chrono::milliseconds(5),
                       [this]() { return !ready_shard_ids_.empty(); });
}

std::mutex& PolicyUpdateQueue::queue_mutex() {
    return queue_mu_;
}

std::uint64_t PolicyUpdateQueue::depth() const {
    std::uint64_t total = 0;
    for (const auto& shard : shards_) {
        std::lock_guard<std::mutex> shard_lock(shard->mu);
        total += shard->jobs.size();
    }
    return total;
}

std::uint64_t PolicyUpdateQueue::capacity() const {
    return static_cast<std::uint64_t>(config_.logical_shard_count) *
           static_cast<std::uint64_t>(config_.max_queue_depth_per_shard);
}

std::optional<std::string> PolicyUpdateQueue::last_enqueued_job_id() const {
    std::lock_guard<std::mutex> lock(queue_mu_);
    if (last_enqueued_job_id_.empty()) {
        return std::nullopt;
    }
    return last_enqueued_job_id_;
}

} // namespace bytetaper::control_plane

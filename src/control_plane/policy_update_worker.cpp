// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_update_worker.h"

#include "control_plane/policy_state_key.h"

#include <chrono>

namespace bytetaper::control_plane {

PolicyUpdateWorker::PolicyUpdateWorker(std::uint32_t worker_id, PolicyUpdateQueue* queue,
                                       PolicyApplyTransactionConfig tx_config)
    : worker_id_(worker_id), queue_(queue), tx_config_(std::move(tx_config)) {}

PolicyUpdateWorker::~PolicyUpdateWorker() {
    stop();
}

void PolicyUpdateWorker::start() {
    if (running_.load()) {
        return;
    }
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread([this]() { worker_loop(); });
}

void PolicyUpdateWorker::stop() {
    stop_.store(true);
    if (queue_ != nullptr) {
        queue_->notify_workers();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
}

bool PolicyUpdateWorker::is_running() const {
    return running_.load();
}

void PolicyUpdateWorker::worker_loop() {
    while (!stop_.load()) {
        PolicyUpdateShard* shard = queue_->try_claim_shard();
        if (shard == nullptr) {
            std::unique_lock<std::mutex> lock(queue_->queue_mutex());
            queue_->wait_for_ready_shard(lock);
            if (stop_.load()) {
                break;
            }
            continue;
        }

        for (;;) {
            PolicyUpdateJob job{};
            {
                std::lock_guard<std::mutex> shard_lock(shard->mu);
                if (shard->jobs.empty()) {
                    break;
                }
                job = std::move(shard->jobs.front());
                shard->jobs.pop_front();
            }

            PolicyApplyTransactionConfig job_tx = tx_config_;
            if (!parse_resource_key(job.resource_key, &job_tx.resource_key)) {
                job_tx.resource_key = PolicyResourceKey::default_runtime();
            }
            const auto user_on_state_change = std::move(job_tx.on_state_change);
            job_tx.on_state_change = [this, user_on_state_change](const PolicyUpdateJob& j) {
                queue_->update_job(j);
                if (user_on_state_change) {
                    user_on_state_change(j);
                }
            };

            PolicyApplyTransaction job_transaction(job_tx);
            const PolicyApplyTransactionResult tx_result = job_transaction.execute(job);
            if (!tx_result.ok && job.state != PolicyUpdateJobState::Failed) {
                job.state = tx_result.final_state;
                queue_->update_job(job);
            }
        }

        queue_->release_shard(shard);
    }
}

} // namespace bytetaper::control_plane

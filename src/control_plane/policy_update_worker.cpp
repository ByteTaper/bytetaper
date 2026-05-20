// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_update_worker.h"

#include "control_plane/control_plane_metrics.h"
#include "control_plane/policy_lifecycle_event.h"
#include "control_plane/policy_state_key.h"

#include <chrono>

namespace bytetaper::control_plane {

PolicyUpdateWorker::PolicyUpdateWorker(std::uint32_t worker_id, PolicyUpdateQueue* queue,
                                       PolicyApplyTransactionConfig tx_config,
                                       operational::PolicyActivationBarrierConfig activation_config)
    : worker_id_(worker_id), queue_(queue), tx_config_(std::move(tx_config)),
      activation_config_(std::move(activation_config)) {}

PolicyUpdateWorker::~PolicyUpdateWorker() {
    stop();
}

void PolicyUpdateWorker::start() {
    if (running_.load()) {
        return;
    }
    stop_.store(false);
    running_.store(true);
    if (tx_config_.control_plane_metrics != nullptr) {
        tx_config_.control_plane_metrics->policy_update_worker_active.fetch_add(
            1, std::memory_order_relaxed);
    }
    thread_ = std::thread([this]() { worker_loop(); });
}

void PolicyUpdateWorker::stop() {
    if (!running_.load()) {
        return;
    }
    stop_.store(true);
    if (queue_ != nullptr) {
        queue_->notify_workers();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
    if (tx_config_.control_plane_metrics != nullptr) {
        tx_config_.control_plane_metrics->policy_update_worker_active.fetch_sub(
            1, std::memory_order_relaxed);
    }
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
            queue_->record_job_dequeued();

            const auto job_start = std::chrono::steady_clock::now();

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
            } else if (tx_result.ok && job.state == PolicyUpdateJobState::Committed &&
                       activation_config_.policy_state_store != nullptr &&
                       activation_config_.runtime_policy_store != nullptr) {
                operational::PolicyActivationBarrierConfig activation_cfg = activation_config_;
                activation_cfg.lifecycle_emitter = job_tx.lifecycle_emitter;
                activation_cfg.runtime_policy_metrics = job_tx.runtime_policy_metrics;
                operational::PolicyActivationBarrier barrier(activation_cfg);
                operational::PolicyActivationRequest activation_req{};
                activation_req.generation = job.candidate_generation;
                activation_req.policy_id = job.candidate_policy_id;
                activation_req.previous_generation = job.expected_base_generation;

                const operational::PolicyActivationResult activation =
                    barrier.activate(activation_req);
                job.activation_status = operational::to_string(activation.status);
                job.activation_stage = operational::to_string(activation.stage);
                job.activation_message = activation.message;
                if (!activation.ok) {
                    job.failure.code = activation.error_code;
                    job.failure.message = activation.message;
                }
                queue_->update_job(job);
            }

            const auto job_end = std::chrono::steady_clock::now();
            const std::uint64_t duration_ms = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(job_end - job_start).count());
            record_policy_update_job(job_tx.control_plane_metrics, tx_result.ok, duration_ms);
        }

        queue_->release_shard(shard);
    }
}

} // namespace bytetaper::control_plane

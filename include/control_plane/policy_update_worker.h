// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_WORKER_H
#define BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_WORKER_H

#include "control_plane/policy_apply_transaction.h"
#include "control_plane/policy_update_queue.h"
#include "operational/policy_activation_barrier.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace bytetaper::control_plane {

class PolicyUpdateWorker {
public:
    PolicyUpdateWorker(std::uint32_t worker_id, PolicyUpdateQueue* queue,
                       PolicyApplyTransactionConfig tx_config,
                       operational::PolicyActivationBarrierConfig activation_config = {});

    ~PolicyUpdateWorker();

    PolicyUpdateWorker(const PolicyUpdateWorker&) = delete;
    PolicyUpdateWorker& operator=(const PolicyUpdateWorker&) = delete;

    void start();
    void stop();

    bool is_running() const;

private:
    void worker_loop();

    std::uint32_t worker_id_;
    PolicyUpdateQueue* queue_;
    PolicyApplyTransactionConfig tx_config_;
    operational::PolicyActivationBarrierConfig activation_config_;
    std::thread thread_;
    std::atomic<bool> stop_{ false };
    std::atomic<bool> running_{ false };
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_WORKER_H

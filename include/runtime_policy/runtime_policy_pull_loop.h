// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_LOOP_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_LOOP_H

#include "control_plane/policy_lifecycle_emitter.h"
#include "operational/policy_activation_barrier.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/control_plane_policy_client.h"
#include "runtime_policy/runtime_policy_pull_config.h"
#include "runtime_policy/runtime_policy_pull_status.h"
#include "runtime_policy/runtime_status_report.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace bytetaper::runtime_policy {

struct RuntimePolicyPullLoopConfig {
    RuntimePolicyPullConfig pull{};
    ControlPlanePolicyClient* client = nullptr;
    operational::PolicyActivationBarrierConfig activation_barrier{};
    control_plane::PolicyLifecycleEmitter* lifecycle_emitter = nullptr;
    RuntimePolicyMetrics* runtime_policy_metrics = nullptr;
    runtime::RuntimePolicyStore* runtime_policy_store = nullptr;
    // When non-null, used instead of build_runtime_policy_snapshot_from_ir (tests).
    operational::PolicySnapshotBuildFn snapshot_build_fn = nullptr;
};

class RuntimePolicyPullLoop {
public:
    explicit RuntimePolicyPullLoop(RuntimePolicyPullLoopConfig config);

    void start();
    void stop();
    bool is_running() const;

    // Runs one pull iteration (used by tests and optional startup sync).
    void tick();

    RuntimePolicyPullStatus status() const;
    RuntimePolicyStatusReport build_report() const;

private:
    void loop_main();
    std::int64_t now_unix_epoch_ms() const;
    std::uint32_t sleep_ms_for_failures() const;
    void update_status(RuntimePolicyPullState state);
    void record_failure(const std::string& code, const std::string& message);
    void reset_failures();
    void sync_status_from_local_snapshot();
    void report_status_to_control_plane();

    RuntimePolicyPullLoopConfig config_;
    mutable std::mutex status_mu_;
    RuntimePolicyPullStatus status_{};
    std::atomic<bool> stop_{ false };
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_LOOP_H

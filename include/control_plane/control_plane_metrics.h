// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_METRICS_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_METRICS_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bytetaper::control_plane {

enum class PolicyApplyStage : std::uint8_t;

static constexpr std::size_t kPolicyApplyStageCount = 8;

struct StageDurationMs {
    std::atomic<std::uint64_t> total{ 0 };
    std::atomic<std::uint64_t> count{ 0 };
};

struct ControlPlaneMetrics {
    std::atomic<std::uint64_t> policy_apply_total{ 0 };
    std::atomic<std::uint64_t> policy_apply_failed_total{ 0 };
    std::atomic<std::uint64_t> policy_apply_duration_ms_total{ 0 };
    std::atomic<std::uint64_t> policy_apply_duration_ms_count{ 0 };
    StageDurationMs policy_apply_stage_duration_ms[kPolicyApplyStageCount]{};
    std::atomic<std::uint64_t> policy_cas_conflict_total{ 0 };
    std::atomic<std::uint64_t> policy_version_stored_total{ 0 };
    std::atomic<std::uint64_t> active_pointer_promote_total{ 0 };
    std::atomic<std::uint64_t> active_pointer_promote_failed_total{ 0 };

    std::atomic<std::uint64_t> policy_update_queue_depth{ 0 };
    std::atomic<std::uint64_t> policy_update_queue_capacity{ 0 };
    std::atomic<std::uint64_t> policy_update_queue_rejected_total{ 0 };
    std::atomic<std::uint64_t> policy_update_job_total{ 0 };
    std::atomic<std::uint64_t> policy_update_job_failed_total{ 0 };
    std::atomic<std::uint64_t> policy_update_job_duration_ms_total{ 0 };
    std::atomic<std::uint64_t> policy_update_job_duration_ms_count{ 0 };
    std::atomic<std::uint64_t> policy_update_worker_active{ 0 };

    std::atomic<std::uint64_t> manual_repair_total{ 0 };
    std::atomic<std::uint64_t> manual_repair_failed_total{ 0 };
    std::atomic<std::uint64_t> manual_adopt_total{ 0 };
    std::atomic<std::uint64_t> manual_adopt_failed_total{ 0 };
    std::atomic<std::uint64_t> policy_rollback_total{ 0 };
    std::atomic<std::uint64_t> policy_rollback_failed_total{ 0 };

    std::atomic<std::uint32_t> fleet_runtime_count{ 0 };
    std::atomic<std::uint32_t> fleet_converged_count{ 0 };
    std::atomic<std::uint32_t> fleet_stale_count{ 0 };
    std::atomic<std::uint32_t> fleet_failed_count{ 0 };
    std::atomic<std::uint32_t> fleet_unreachable_count{ 0 };
    std::atomic<std::uint32_t> fleet_degraded_count{ 0 };
    std::atomic<std::uint32_t> fleet_converged{ 0 };

    std::atomic<std::uint64_t> auth_requests_total{ 0 };
    std::atomic<std::uint64_t> auth_denied_total{ 0 };
    std::atomic<std::uint64_t> mutation_rejected_total{ 0 };
    std::atomic<std::uint64_t> public_bind_rejected_total{ 0 };
    std::atomic<std::uint64_t> unsafe_config_total{ 0 };
    std::atomic<std::uint64_t> runtime_only_mutation_rejected_total{ 0 };
};

void record_policy_apply_success(ControlPlaneMetrics* metrics, std::uint64_t duration_ms);
void record_policy_apply_failure(ControlPlaneMetrics* metrics, const char* stage,
                                 const char* reason);
std::size_t policy_apply_stage_index(PolicyApplyStage stage);
void record_policy_apply_stage_duration(ControlPlaneMetrics* metrics, std::size_t stage_index,
                                        std::uint64_t duration_ms);
void record_policy_cas_conflict(ControlPlaneMetrics* metrics);
void record_queue_rejection(ControlPlaneMetrics* metrics);
void record_policy_update_job(ControlPlaneMetrics* metrics, bool success,
                              std::uint64_t duration_ms);
void record_manual_operation(ControlPlaneMetrics* metrics, const char* operation, bool success);
void update_fleet_metrics(ControlPlaneMetrics* metrics, std::uint32_t runtime_count,
                          std::uint32_t converged_count, std::uint32_t stale_count,
                          std::uint32_t failed_count, std::uint32_t unreachable_count,
                          std::uint32_t degraded_count, bool fleet_converged);
void record_unsafe_config(ControlPlaneMetrics* metrics);

std::size_t render_control_plane_metrics_prometheus(const ControlPlaneMetrics& metrics, char* buf,
                                                    std::size_t buf_size);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_METRICS_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_METRICS_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_METRICS_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bytetaper::operational {
enum class PolicyActivationStage : std::uint8_t;
}

namespace bytetaper::runtime_policy {

static constexpr std::size_t kPolicyActivationStageCount = 10;

struct StageDurationMs {
    std::atomic<std::uint64_t> total{ 0 };
    std::atomic<std::uint64_t> count{ 0 };
};

struct RuntimePolicyMetrics {
    std::atomic<std::uint64_t> activation_total{ 0 };
    std::atomic<std::uint64_t> activation_failed_total{ 0 };
    std::atomic<std::uint64_t> activation_duration_ms_total{ 0 };
    std::atomic<std::uint64_t> activation_duration_ms_count{ 0 };
    StageDurationMs activation_stage_duration_ms[kPolicyActivationStageCount]{};
    std::atomic<std::uint64_t> active_generation{ 0 };
    std::atomic<std::uint64_t> activation_status{ 0 };
    std::atomic<std::uint64_t> cleanup_pending{ 0 };

    std::atomic<std::uint64_t> route_epochs_bumped_total{ 0 };
    std::atomic<std::uint64_t> l1_cleanup_enqueued_total{ 0 };
    std::atomic<std::uint64_t> l2_cleanup_enqueued_total{ 0 };
    std::atomic<std::uint64_t> materialized_variant_invalidated_total{ 0 };

    std::atomic<std::uint64_t> mismatch_total{ 0 };
    std::atomic<std::uint64_t> corrupted_local_total{ 0 };
    std::atomic<std::uint64_t> stale_local_total{ 0 };
    std::atomic<std::uint64_t> diverged_local_total{ 0 };
    std::atomic<std::uint64_t> missing_local_total{ 0 };
    std::atomic<std::uint64_t> unparseable_local_total{ 0 };
    std::atomic<std::uint64_t> bootstrap_drift_total{ 0 };

    std::atomic<std::uint64_t> runtime_converged_total{ 0 };
    std::atomic<std::uint64_t> runtime_stale_total{ 0 };
    std::atomic<std::uint64_t> runtime_activation_failed_total{ 0 };
};

void record_activation_success(RuntimePolicyMetrics* metrics, std::uint64_t duration_ms,
                               std::uint64_t generation);
void record_activation_operational_counts(RuntimePolicyMetrics* metrics,
                                          std::uint32_t bumped_route_epochs,
                                          std::uint32_t l1_cleanup_jobs,
                                          std::uint32_t l2_cleanup_jobs,
                                          std::uint32_t materialized_variant_invalidations,
                                          bool cleanup_pending);
void record_activation_failure(RuntimePolicyMetrics* metrics, const char* stage);
std::size_t policy_activation_stage_index(operational::PolicyActivationStage stage);
void record_activation_stage_duration(RuntimePolicyMetrics* metrics, std::size_t stage_index,
                                      std::uint64_t duration_ms);
void record_mismatch(RuntimePolicyMetrics* metrics, const char* reason);
void record_runtime_convergence(RuntimePolicyMetrics* metrics, const char* status);

std::size_t render_runtime_policy_metrics_prometheus(const RuntimePolicyMetrics& metrics, char* buf,
                                                     std::size_t buf_size);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_METRICS_H

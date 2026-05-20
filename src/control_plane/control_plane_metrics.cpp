// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_metrics.h"

#include "control_plane/policy_apply_transaction.h"

#include <cstdio>
#include <cstring>

namespace bytetaper::control_plane {

namespace {

void append_metric_line(char* buf, std::size_t buf_size, std::size_t* offset, const char* name,
                        std::uint64_t value) {
    if (buf == nullptr || offset == nullptr || *offset >= buf_size) {
        return;
    }
    const int written = std::snprintf(buf + *offset, buf_size - *offset, "%s %llu\n", name,
                                      static_cast<unsigned long long>(value));
    if (written > 0) {
        *offset += static_cast<std::size_t>(written);
    }
}

} // namespace

std::size_t policy_apply_stage_index(PolicyApplyStage stage) {
    switch (stage) {
    case PolicyApplyStage::LoadActive:
        return 0;
    case PolicyApplyStage::ValidateBase:
        return 1;
    case PolicyApplyStage::LoadBasePolicy:
        return 2;
    case PolicyApplyStage::BuildCandidate:
        return 3;
    case PolicyApplyStage::Canonicalize:
        return 4;
    case PolicyApplyStage::StoreVersion:
        return 5;
    case PolicyApplyStage::CompareAndPromote:
        return 6;
    case PolicyApplyStage::RecordResult:
        return 7;
    }
    return 0;
}

void record_policy_apply_stage_duration(ControlPlaneMetrics* metrics, std::size_t stage_index,
                                        std::uint64_t duration_ms) {
    if (metrics == nullptr || stage_index >= kPolicyApplyStageCount) {
        return;
    }
    metrics->policy_apply_stage_duration_ms[stage_index].total.fetch_add(duration_ms,
                                                                         std::memory_order_relaxed);
    metrics->policy_apply_stage_duration_ms[stage_index].count.fetch_add(1,
                                                                         std::memory_order_relaxed);
}

void record_policy_apply_success(ControlPlaneMetrics* metrics, std::uint64_t duration_ms) {
    if (metrics == nullptr) {
        return;
    }
    metrics->policy_apply_total.fetch_add(1, std::memory_order_relaxed);
    metrics->policy_apply_duration_ms_total.fetch_add(duration_ms, std::memory_order_relaxed);
    metrics->policy_apply_duration_ms_count.fetch_add(1, std::memory_order_relaxed);
}

void record_policy_apply_failure(ControlPlaneMetrics* metrics, const char* stage,
                                 const char* reason) {
    (void) stage;
    (void) reason;
    if (metrics == nullptr) {
        return;
    }
    metrics->policy_apply_failed_total.fetch_add(1, std::memory_order_relaxed);
}

void record_policy_cas_conflict(ControlPlaneMetrics* metrics) {
    if (metrics == nullptr) {
        return;
    }
    metrics->policy_cas_conflict_total.fetch_add(1, std::memory_order_relaxed);
}

void record_queue_rejection(ControlPlaneMetrics* metrics) {
    if (metrics == nullptr) {
        return;
    }
    metrics->policy_update_queue_rejected_total.fetch_add(1, std::memory_order_relaxed);
}

void record_policy_update_job(ControlPlaneMetrics* metrics, bool success,
                              std::uint64_t duration_ms) {
    if (metrics == nullptr) {
        return;
    }
    metrics->policy_update_job_total.fetch_add(1, std::memory_order_relaxed);
    if (!success) {
        metrics->policy_update_job_failed_total.fetch_add(1, std::memory_order_relaxed);
    }
    metrics->policy_update_job_duration_ms_total.fetch_add(duration_ms, std::memory_order_relaxed);
    metrics->policy_update_job_duration_ms_count.fetch_add(1, std::memory_order_relaxed);
}

void record_manual_operation(ControlPlaneMetrics* metrics, const char* operation, bool success) {
    if (metrics == nullptr || operation == nullptr) {
        return;
    }
    if (std::strcmp(operation, "repair-local") == 0) {
        metrics->manual_repair_total.fetch_add(1, std::memory_order_relaxed);
        if (!success) {
            metrics->manual_repair_failed_total.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (std::strcmp(operation, "adopt-local") == 0) {
        metrics->manual_adopt_total.fetch_add(1, std::memory_order_relaxed);
        if (!success) {
            metrics->manual_adopt_failed_total.fetch_add(1, std::memory_order_relaxed);
        }
    } else if (std::strcmp(operation, "rollback") == 0) {
        metrics->policy_rollback_total.fetch_add(1, std::memory_order_relaxed);
        if (!success) {
            metrics->policy_rollback_failed_total.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void record_unsafe_config(ControlPlaneMetrics* metrics) {
    if (metrics == nullptr) {
        return;
    }
    metrics->unsafe_config_total.fetch_add(1, std::memory_order_relaxed);
}

void update_fleet_metrics(ControlPlaneMetrics* metrics, std::uint32_t runtime_count,
                          std::uint32_t converged_count, std::uint32_t stale_count,
                          std::uint32_t failed_count, std::uint32_t unreachable_count,
                          std::uint32_t degraded_count, bool fleet_converged) {
    if (metrics == nullptr) {
        return;
    }
    metrics->fleet_runtime_count.store(runtime_count, std::memory_order_relaxed);
    metrics->fleet_converged_count.store(converged_count, std::memory_order_relaxed);
    metrics->fleet_stale_count.store(stale_count, std::memory_order_relaxed);
    metrics->fleet_failed_count.store(failed_count, std::memory_order_relaxed);
    metrics->fleet_unreachable_count.store(unreachable_count, std::memory_order_relaxed);
    metrics->fleet_degraded_count.store(degraded_count, std::memory_order_relaxed);
    metrics->fleet_converged.store(fleet_converged ? 1u : 0u, std::memory_order_relaxed);
}

std::size_t render_control_plane_metrics_prometheus(const ControlPlaneMetrics& metrics, char* buf,
                                                    std::size_t buf_size) {
    if (buf == nullptr || buf_size == 0) {
        return 0;
    }
    std::size_t offset = 0;
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_policy_apply_total",
                       metrics.policy_apply_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_policy_apply_failed_total",
                       metrics.policy_apply_failed_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_policy_apply_duration_ms",
                       metrics.policy_apply_duration_ms_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_policy_cas_conflict_total",
                       metrics.policy_cas_conflict_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_policy_version_stored_total",
                       metrics.policy_version_stored_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_active_pointer_promote_total",
                       metrics.active_pointer_promote_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_active_pointer_promote_failed_total",
                       metrics.active_pointer_promote_failed_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_policy_update_queue_depth",
                       metrics.policy_update_queue_depth.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_policy_update_queue_capacity",
                       metrics.policy_update_queue_capacity.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_policy_update_queue_rejected_total",
                       metrics.policy_update_queue_rejected_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_policy_update_job_total",
                       metrics.policy_update_job_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_policy_update_job_failed_total",
                       metrics.policy_update_job_failed_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_policy_update_job_duration_ms",
                       metrics.policy_update_job_duration_ms_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_policy_update_worker_active",
                       metrics.policy_update_worker_active.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_manual_repair_total",
                       metrics.manual_repair_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_manual_repair_failed_total",
                       metrics.manual_repair_failed_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_manual_adopt_total",
                       metrics.manual_adopt_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_manual_adopt_failed_total",
                       metrics.manual_adopt_failed_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_policy_rollback_total",
                       metrics.policy_rollback_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_policy_rollback_failed_total",
                       metrics.policy_rollback_failed_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_fleet_runtime_count",
                       metrics.fleet_runtime_count.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_fleet_converged_count",
                       metrics.fleet_converged_count.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_fleet_stale_count",
                       metrics.fleet_stale_count.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_fleet_failed_count",
                       metrics.fleet_failed_count.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_fleet_unreachable_count",
                       metrics.fleet_unreachable_count.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_fleet_degraded_count",
                       metrics.fleet_degraded_count.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_fleet_converged",
                       metrics.fleet_converged.load());
    for (std::size_t i = 0; i < kPolicyApplyStageCount; ++i) {
        if (metrics.policy_apply_stage_duration_ms[i].count.load() > 0) {
            append_metric_line(buf, buf_size, &offset,
                               "bytetaper_control_plane_policy_apply_stage_duration_ms",
                               metrics.policy_apply_stage_duration_ms[i].total.load());
        }
    }
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_auth_requests_total",
                       metrics.auth_requests_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_auth_denied_total",
                       metrics.auth_denied_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_mutation_rejected_total",
                       metrics.mutation_rejected_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_public_bind_rejected_total",
                       metrics.public_bind_rejected_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_control_plane_unsafe_config_total",
                       metrics.unsafe_config_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_control_plane_runtime_only_mutation_rejected_total",
                       metrics.runtime_only_mutation_rejected_total.load());
    return offset;
}

} // namespace bytetaper::control_plane

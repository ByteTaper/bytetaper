// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_policy_metrics.h"

#include "operational/policy_activation_result.h"

#include <cstdio>
#include <cstring>

namespace bytetaper::runtime_policy {

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

void increment_mismatch_reason(RuntimePolicyMetrics* metrics, const char* reason) {
    if (metrics == nullptr || reason == nullptr) {
        return;
    }
    metrics->mismatch_total.fetch_add(1, std::memory_order_relaxed);
    if (std::strcmp(reason, "local_policy_corrupted_or_modified") == 0 ||
        std::strcmp(reason, "corrupted") == 0) {
        metrics->corrupted_local_total.fetch_add(1, std::memory_order_relaxed);
    } else if (std::strcmp(reason, "stale") == 0 || std::strcmp(reason, "runtime_stale") == 0) {
        metrics->stale_local_total.fetch_add(1, std::memory_order_relaxed);
    } else if (std::strcmp(reason, "diverged") == 0) {
        metrics->diverged_local_total.fetch_add(1, std::memory_order_relaxed);
    } else if (std::strcmp(reason, "missing") == 0) {
        metrics->missing_local_total.fetch_add(1, std::memory_order_relaxed);
    } else if (std::strcmp(reason, "unparseable") == 0) {
        metrics->unparseable_local_total.fetch_add(1, std::memory_order_relaxed);
    } else if (std::strcmp(reason, "bootstrap_drift") == 0) {
        metrics->bootstrap_drift_total.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

std::size_t policy_activation_stage_index(operational::PolicyActivationStage stage) {
    switch (stage) {
    case operational::PolicyActivationStage::Committed:
        return 0;
    case operational::PolicyActivationStage::OperationalDiffComputed:
        return 1;
    case operational::PolicyActivationStage::CacheNamespacePrepared:
        return 2;
    case operational::PolicyActivationStage::RouteEpochsBumped:
        return 3;
    case operational::PolicyActivationStage::CleanupEnqueued:
        return 4;
    case operational::PolicyActivationStage::MaterializedVariantsInvalidated:
        return 5;
    case operational::PolicyActivationStage::SnapshotBuilt:
        return 6;
    case operational::PolicyActivationStage::SnapshotSwapped:
        return 7;
    case operational::PolicyActivationStage::ActivationPublished:
        return 8;
    case operational::PolicyActivationStage::Activated:
        return 9;
    }
    return 0;
}

void record_activation_stage_duration(RuntimePolicyMetrics* metrics, std::size_t stage_index,
                                      std::uint64_t duration_ms) {
    if (metrics == nullptr || stage_index >= kPolicyActivationStageCount) {
        return;
    }
    metrics->activation_stage_duration_ms[stage_index].total.fetch_add(duration_ms,
                                                                       std::memory_order_relaxed);
    metrics->activation_stage_duration_ms[stage_index].count.fetch_add(1,
                                                                       std::memory_order_relaxed);
}

void record_activation_success(RuntimePolicyMetrics* metrics, std::uint64_t duration_ms,
                               std::uint64_t generation) {
    if (metrics == nullptr) {
        return;
    }
    metrics->activation_total.fetch_add(1, std::memory_order_relaxed);
    metrics->activation_duration_ms_total.fetch_add(duration_ms, std::memory_order_relaxed);
    metrics->activation_duration_ms_count.fetch_add(1, std::memory_order_relaxed);
    metrics->active_generation.store(generation, std::memory_order_relaxed);
    metrics->activation_status.store(1, std::memory_order_relaxed);
}

void record_activation_operational_counts(RuntimePolicyMetrics* metrics,
                                          std::uint32_t bumped_route_epochs,
                                          std::uint32_t l1_cleanup_jobs,
                                          std::uint32_t l2_cleanup_jobs,
                                          std::uint32_t materialized_variant_invalidations,
                                          bool cleanup_pending) {
    if (metrics == nullptr) {
        return;
    }
    if (bumped_route_epochs > 0) {
        metrics->route_epochs_bumped_total.fetch_add(bumped_route_epochs,
                                                     std::memory_order_relaxed);
    }
    if (l1_cleanup_jobs > 0) {
        metrics->l1_cleanup_enqueued_total.fetch_add(l1_cleanup_jobs, std::memory_order_relaxed);
    }
    if (l2_cleanup_jobs > 0) {
        metrics->l2_cleanup_enqueued_total.fetch_add(l2_cleanup_jobs, std::memory_order_relaxed);
    }
    if (materialized_variant_invalidations > 0) {
        metrics->materialized_variant_invalidated_total.fetch_add(
            materialized_variant_invalidations, std::memory_order_relaxed);
    }
    metrics->cleanup_pending.store(cleanup_pending ? 1u : 0u, std::memory_order_relaxed);
}

void record_activation_failure(RuntimePolicyMetrics* metrics, const char* stage) {
    (void) stage;
    if (metrics == nullptr) {
        return;
    }
    metrics->activation_failed_total.fetch_add(1, std::memory_order_relaxed);
    metrics->activation_status.store(2, std::memory_order_relaxed);
}

void record_mismatch(RuntimePolicyMetrics* metrics, const char* reason) {
    increment_mismatch_reason(metrics, reason);
}

void record_runtime_convergence(RuntimePolicyMetrics* metrics, const char* status) {
    if (metrics == nullptr || status == nullptr) {
        return;
    }
    if (std::strcmp(status, "converged") == 0) {
        metrics->runtime_converged_total.fetch_add(1, std::memory_order_relaxed);
    } else if (std::strcmp(status, "stale") == 0) {
        metrics->runtime_stale_total.fetch_add(1, std::memory_order_relaxed);
    } else if (std::strcmp(status, "failed") == 0) {
        metrics->runtime_activation_failed_total.fetch_add(1, std::memory_order_relaxed);
    }
}

std::size_t render_runtime_policy_metrics_prometheus(const RuntimePolicyMetrics& metrics, char* buf,
                                                     std::size_t buf_size) {
    if (buf == nullptr || buf_size == 0) {
        return 0;
    }
    std::size_t offset = 0;
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_activation_total",
                       metrics.activation_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_activation_failed_total",
                       metrics.activation_failed_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_activation_duration_ms",
                       metrics.activation_duration_ms_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_active_generation",
                       metrics.active_generation.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_activation_status",
                       metrics.activation_status.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_cleanup_pending",
                       metrics.cleanup_pending.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_route_epochs_bumped_total",
                       metrics.route_epochs_bumped_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_l1_cleanup_enqueued_total",
                       metrics.l1_cleanup_enqueued_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_l2_cleanup_enqueued_total",
                       metrics.l2_cleanup_enqueued_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_runtime_policy_materialized_variant_invalidated_total",
                       metrics.materialized_variant_invalidated_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_mismatch_total",
                       metrics.mismatch_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_corrupted_local_total",
                       metrics.corrupted_local_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_stale_local_total",
                       metrics.stale_local_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_diverged_local_total",
                       metrics.diverged_local_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_missing_local_total",
                       metrics.missing_local_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_unparseable_local_total",
                       metrics.unparseable_local_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_bootstrap_drift_total",
                       metrics.bootstrap_drift_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_runtime_converged_total",
                       metrics.runtime_converged_total.load());
    append_metric_line(buf, buf_size, &offset, "bytetaper_runtime_policy_runtime_stale_total",
                       metrics.runtime_stale_total.load());
    append_metric_line(buf, buf_size, &offset,
                       "bytetaper_runtime_policy_runtime_activation_failed_total",
                       metrics.runtime_activation_failed_total.load());
    for (std::size_t i = 0; i < kPolicyActivationStageCount; ++i) {
        if (metrics.activation_stage_duration_ms[i].count.load() > 0) {
            append_metric_line(buf, buf_size, &offset,
                               "bytetaper_runtime_policy_activation_stage_duration_ms",
                               metrics.activation_stage_duration_ms[i].total.load());
        }
    }
    return offset;
}

} // namespace bytetaper::runtime_policy

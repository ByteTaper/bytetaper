// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/fleet_status_service.h"

#include "control_plane/policy_state_result.h"

namespace bytetaper::control_plane {

FleetStatusService::FleetStatusService(FleetStatusConfig config,
                                       PolicyStateStore* policy_state_store)
    : config_(std::move(config)), policy_state_store_(policy_state_store) {}

RuntimeStatusReportResult FleetStatusService::ingest(const RuntimeStatusReport& report,
                                                     std::int64_t now_unix_epoch_ms) {
    RuntimeStatusReportResult result{};
    std::string validation_error;
    if (!validate_runtime_status_report(report, &validation_error)) {
        result.error = validation_error;
        result.error_code = kErrRuntimeStatusInvalid;
        return result;
    }

    PolicyResourceKey parsed{};
    if (!parse_resource_key(report.resource_key, &parsed)) {
        result.error = "resourceKey is invalid";
        result.error_code = kErrRuntimeStatusResourceUnknown;
        return result;
    }

    RuntimeStatusReport stored = report;
    if (stored.received_at_unix_epoch_ms <= 0) {
        stored.received_at_unix_epoch_ms = now_unix_epoch_ms;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        runtimes_by_resource_[stored.resource_key][stored.runtime_id] = std::move(stored);
    }

    result.ok = true;
    return result;
}

FleetStatusResult FleetStatusService::get_fleet_status(const PolicyResourceKey& resource_key,
                                                       std::int64_t now_unix_epoch_ms) {
    FleetStatusResult result{};
    result.status.resource_key = resource_key.to_string();

    if (policy_state_store_ == nullptr) {
        result.error = "policy state store is not configured";
        result.error_code = kErrFleetStatusActivePointerMissing;
        return result;
    }

    const LoadActivePointerResult active = policy_state_store_->load_active_pointer(resource_key);
    if (!active.ok) {
        result.error = active.error.empty() ? "no committed active policy pointer" : active.error;
        result.error_code = kErrFleetStatusActivePointerMissing;
        return result;
    }

    prune_expired(now_unix_epoch_ms);

    result.status.committed.generation = active.pointer.generation;
    result.status.committed.policy_id = active.pointer.policy_id;
    result.status.committed.canonical_hash = active.pointer.canonical_hash;

    std::map<std::string, RuntimeStatusReport> runtimes;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = runtimes_by_resource_.find(result.status.resource_key);
        if (it != runtimes_by_resource_.end()) {
            runtimes = it->second;
        }
    }

    result.status.fleet.runtime_count = static_cast<std::uint32_t>(runtimes.size());
    result.status.runtimes.reserve(runtimes.size());

    for (const auto& entry : runtimes) {
        const RuntimeStatusReport& report = entry.second;
        const RuntimeConvergenceStatus convergence =
            classify_runtime_convergence(active.pointer, report, now_unix_epoch_ms, config_);

        RuntimeFleetEntry fleet_entry{};
        fleet_entry.runtime_id = report.runtime_id;
        fleet_entry.gateway_adapter = report.gateway_adapter;
        fleet_entry.active_generation = report.active_generation;
        fleet_entry.active_policy_id = report.active_policy_id;
        fleet_entry.active_canonical_hash = report.active_canonical_hash;
        fleet_entry.activation_status = report.activation_status;
        fleet_entry.convergence_status = convergence;
        if (const char* code = convergence_status_error_code(convergence); code != nullptr) {
            fleet_entry.convergence_error_code = code;
        }
        fleet_entry.data_path_mode = report.data_path_mode;
        fleet_entry.control_plane_reachable = report.control_plane_reachable;
        fleet_entry.last_pull_at_unix_epoch_ms = report.last_pull_at_unix_epoch_ms;
        fleet_entry.last_activated_at_unix_epoch_ms = report.last_activated_at_unix_epoch_ms;
        result.status.runtimes.push_back(std::move(fleet_entry));

        switch (convergence) {
        case RuntimeConvergenceStatus::Converged:
            result.status.fleet.converged_count++;
            break;
        case RuntimeConvergenceStatus::Stale:
            result.status.fleet.stale_count++;
            break;
        case RuntimeConvergenceStatus::Divergent:
            result.status.fleet.divergent_count++;
            break;
        case RuntimeConvergenceStatus::Failed:
            result.status.fleet.failed_count++;
            break;
        case RuntimeConvergenceStatus::Unreachable:
            result.status.fleet.unreachable_count++;
            break;
        case RuntimeConvergenceStatus::Degraded:
            result.status.fleet.degraded_count++;
            break;
        case RuntimeConvergenceStatus::Unknown:
        default:
            break;
        }
    }

    result.status.fleet.converged =
        result.status.fleet.runtime_count > 0 && result.status.fleet.stale_count == 0 &&
        result.status.fleet.failed_count == 0 && result.status.fleet.unreachable_count == 0 &&
        result.status.fleet.divergent_count == 0 && result.status.fleet.degraded_count == 0 &&
        result.status.fleet.converged_count == result.status.fleet.runtime_count;

    result.status.ok = true;
    result.ok = true;
    return result;
}

void FleetStatusService::prune_expired(std::int64_t now_unix_epoch_ms) {
    if (config_.runtime_status_retention_ms == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mu_);
    for (auto resource_it = runtimes_by_resource_.begin();
         resource_it != runtimes_by_resource_.end();) {
        auto& runtimes = resource_it->second;
        for (auto runtime_it = runtimes.begin(); runtime_it != runtimes.end();) {
            const RuntimeStatusReport& report = runtime_it->second;
            if (report.received_at_unix_epoch_ms > 0 &&
                now_unix_epoch_ms > report.received_at_unix_epoch_ms &&
                static_cast<std::uint64_t>(now_unix_epoch_ms - report.received_at_unix_epoch_ms) >
                    config_.runtime_status_retention_ms) {
                runtime_it = runtimes.erase(runtime_it);
            } else {
                ++runtime_it;
            }
        }
        if (runtimes.empty()) {
            resource_it = runtimes_by_resource_.erase(resource_it);
        } else {
            ++resource_it;
        }
    }
}

} // namespace bytetaper::control_plane

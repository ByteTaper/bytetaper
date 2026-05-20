// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_FLEET_STATUS_H
#define BYTETAPER_CONTROL_PLANE_FLEET_STATUS_H

#include "control_plane/runtime_convergence_status.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::control_plane {

struct CommittedPolicySummary {
    std::uint64_t generation = 0;
    std::string policy_id;
    std::string canonical_hash;
};

struct RuntimeFleetEntry {
    std::string runtime_id;
    std::string gateway_adapter;
    std::uint64_t active_generation = 0;
    std::string active_policy_id;
    std::string active_canonical_hash;
    std::string activation_status;
    RuntimeConvergenceStatus convergence_status = RuntimeConvergenceStatus::Unknown;
    std::string convergence_error_code;
    std::string data_path_mode;
    bool control_plane_reachable = false;
    std::int64_t last_pull_at_unix_epoch_ms = 0;
    std::int64_t last_activated_at_unix_epoch_ms = 0;
};

struct RuntimeFleetSummary {
    std::uint32_t runtime_count = 0;
    std::uint32_t converged_count = 0;
    std::uint32_t stale_count = 0;
    std::uint32_t divergent_count = 0;
    std::uint32_t failed_count = 0;
    std::uint32_t unreachable_count = 0;
    std::uint32_t degraded_count = 0;
    bool converged = false;
};

struct FleetPolicyObservability {
    std::string last_apply_job_id;
    std::string last_apply_status;
    std::string last_activation_status;
    std::string last_failure_code;
    std::string last_failure_stage;
    std::uint64_t queue_depth = 0;
    std::uint64_t queue_capacity = 0;
    bool cleanup_pending = false;
};

struct FleetPolicyStatus {
    bool ok = false;
    std::string resource_key;
    CommittedPolicySummary committed{};
    RuntimeFleetSummary fleet{};
    FleetPolicyObservability observability{};
    std::vector<RuntimeFleetEntry> runtimes;
    std::string error;
    std::string error_code;
};

struct FleetStatusResult {
    bool ok = false;
    FleetPolicyStatus status{};
    std::string error;
    std::string error_code;
};

constexpr const char* kErrFleetStatusActivePointerMissing = "FLEET_STATUS_ACTIVE_POINTER_MISSING";

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_FLEET_STATUS_H

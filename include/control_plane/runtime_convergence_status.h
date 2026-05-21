// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_RUNTIME_CONVERGENCE_STATUS_H
#define BYTETAPER_CONTROL_PLANE_RUNTIME_CONVERGENCE_STATUS_H

#include "control_plane/fleet_status_config.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/runtime_status_report.h"

#include <cstdint>

namespace bytetaper::control_plane {

enum class RuntimeConvergenceStatus : std::uint8_t {
    Converged,
    Stale,
    Divergent,
    Failed,
    Degraded,
    Unreachable,
    Unknown,
};

// Spec error codes for convergence outcomes.
constexpr const char* kErrRuntimeStatusPolicyMismatch = "RUNTIME_STATUS_POLICY_MISMATCH";
constexpr const char* kErrRuntimeStatusStale = "RUNTIME_STATUS_STALE";
constexpr const char* kErrRuntimeStatusUnreachable = "RUNTIME_STATUS_UNREACHABLE";

const char* to_string(RuntimeConvergenceStatus status);

// Returns a spec error code for Stale/Divergent/Unreachable; nullptr otherwise.
const char* convergence_status_error_code(RuntimeConvergenceStatus status);

RuntimeConvergenceStatus classify_runtime_convergence(const ActivePolicyPointer& committed,
                                                      const RuntimeStatusReport& report,
                                                      std::int64_t now_unix_epoch_ms,
                                                      const FleetStatusConfig& config);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_RUNTIME_CONVERGENCE_STATUS_H

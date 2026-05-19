// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/runtime_convergence_status.h"

#include <algorithm>
#include <cctype>

namespace bytetaper::control_plane {

namespace {

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalize_token(std::string value) {
    value = to_lower(std::move(value));
    for (char& c : value) {
        if (c == '-' || c == ' ') {
            c = '_';
        }
    }
    return value;
}

std::string normalize_hash(const std::string& hash) {
    if (hash.rfind("sha256:", 0) == 0) {
        return hash;
    }
    if (hash.empty()) {
        return hash;
    }
    return "sha256:" + hash;
}

bool activation_is_active_or_cleanup_pending(const std::string& activation_status) {
    const std::string normalized = normalize_token(activation_status);
    return normalized == "active" || normalized == "cleanup_pending" ||
           normalized == "policyactive" || normalized == "policyactivecleanuppending";
}

bool activation_is_failed(const std::string& activation_status) {
    const std::string normalized = normalize_token(activation_status);
    return normalized == "activation_failed" || normalized == "policy_inactive" ||
           normalized == "policyactivationfailed" || normalized == "policyinactive";
}

bool data_path_is_degraded(const std::string& data_path_mode) {
    const std::string normalized = normalize_token(data_path_mode);
    return normalized == "pass_through" || normalized == "passthrough" ||
           normalized == "degraded_pass_through";
}

} // namespace

const char* to_string(RuntimeConvergenceStatus status) {
    switch (status) {
    case RuntimeConvergenceStatus::Converged:
        return "converged";
    case RuntimeConvergenceStatus::Stale:
        return "stale";
    case RuntimeConvergenceStatus::Divergent:
        return "divergent";
    case RuntimeConvergenceStatus::Failed:
        return "failed";
    case RuntimeConvergenceStatus::Degraded:
        return "degraded";
    case RuntimeConvergenceStatus::Unreachable:
        return "unreachable";
    case RuntimeConvergenceStatus::Unknown:
    default:
        return "unknown";
    }
}

const char* convergence_status_error_code(RuntimeConvergenceStatus status) {
    switch (status) {
    case RuntimeConvergenceStatus::Stale:
        return kErrRuntimeStatusStale;
    case RuntimeConvergenceStatus::Divergent:
        return kErrRuntimeStatusPolicyMismatch;
    case RuntimeConvergenceStatus::Unreachable:
        return kErrRuntimeStatusUnreachable;
    case RuntimeConvergenceStatus::Converged:
    case RuntimeConvergenceStatus::Failed:
    case RuntimeConvergenceStatus::Degraded:
    case RuntimeConvergenceStatus::Unknown:
    default:
        return nullptr;
    }
}

RuntimeConvergenceStatus classify_runtime_convergence(const ActivePolicyPointer& committed,
                                                      const RuntimeStatusReport& report,
                                                      std::int64_t now_unix_epoch_ms,
                                                      const FleetStatusConfig& config) {
    if (report.received_at_unix_epoch_ms > 0 &&
        now_unix_epoch_ms > report.received_at_unix_epoch_ms &&
        static_cast<std::uint64_t>(now_unix_epoch_ms - report.received_at_unix_epoch_ms) >
            config.runtime_heartbeat_timeout_ms) {
        return RuntimeConvergenceStatus::Unreachable;
    }

    if (!report.control_plane_reachable) {
        const std::string activation = normalize_token(report.activation_status);
        if (activation == "control_plane_unavailable" ||
            activation == "degradedcontrolplaneunavailable") {
            return RuntimeConvergenceStatus::Unreachable;
        }
    }

    if (activation_is_failed(report.activation_status)) {
        return RuntimeConvergenceStatus::Failed;
    }

    if (data_path_is_degraded(report.data_path_mode)) {
        return RuntimeConvergenceStatus::Degraded;
    }

    const std::string normalized_activation = normalize_token(report.activation_status);
    if (normalized_activation == "degraded_pass_through") {
        return RuntimeConvergenceStatus::Degraded;
    }

    if (report.active_generation < committed.generation) {
        return RuntimeConvergenceStatus::Stale;
    }

    const bool generation_matches = report.active_generation == committed.generation;
    const bool policy_matches = report.active_policy_id == committed.policy_id;
    const bool hash_matches =
        normalize_hash(report.active_canonical_hash) == normalize_hash(committed.canonical_hash);

    if (generation_matches && (!policy_matches || !hash_matches)) {
        return RuntimeConvergenceStatus::Divergent;
    }

    if (generation_matches && policy_matches && hash_matches &&
        activation_is_active_or_cleanup_pending(report.activation_status)) {
        return RuntimeConvergenceStatus::Converged;
    }

    return RuntimeConvergenceStatus::Unknown;
}

} // namespace bytetaper::control_plane

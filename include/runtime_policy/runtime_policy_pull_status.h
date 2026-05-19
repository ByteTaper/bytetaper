// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_STATUS_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_STATUS_H

#include <cstdint>
#include <string>

namespace bytetaper::runtime_policy {

enum class RuntimePolicyPullState : std::uint8_t {
    Stopped,
    Starting,
    Idle,
    CheckingControlPlane,
    FetchingPolicy,
    ValidatingPolicy,
    ActivatingPolicy,
    Active,
    DegradedControlPlaneUnavailable,
    ActivationFailed,
    PolicyInactive,
};

struct RuntimePolicyPullStatus {
    RuntimePolicyPullState state = RuntimePolicyPullState::Stopped;
    std::uint64_t active_generation = 0;
    std::string active_policy_id;
    std::string active_canonical_hash;
    std::string activation_status;
    std::int64_t last_successful_activation_at_unix_epoch_ms = 0;
    std::int64_t last_control_plane_check_at_unix_epoch_ms = 0;
    std::string last_control_plane_error;
    std::string last_error_code;
    std::uint32_t consecutive_failures = 0;
    bool control_plane_reachable = false;
};

const char* to_string(RuntimePolicyPullState state);

// Error codes reserved for pull-loop failures.
constexpr const char* kErrControlPlaneUnavailable = "CONTROL_PLANE_UNAVAILABLE";
constexpr const char* kErrActivePolicyQueryFailed = "ACTIVE_POLICY_QUERY_FAILED";
constexpr const char* kErrPolicyVersionFetchFailed = "POLICY_VERSION_FETCH_FAILED";
constexpr const char* kErrPolicyCanonicalHashMismatch = "POLICY_CANONICAL_HASH_MISMATCH";
constexpr const char* kErrPolicyIdMismatch = "POLICY_ID_MISMATCH";
constexpr const char* kErrPolicyGenerationMismatch = "POLICY_GENERATION_MISMATCH";
constexpr const char* kErrPolicyCompileFailed = "POLICY_COMPILE_FAILED";
constexpr const char* kErrPolicyActivationFailed = "POLICY_ACTIVATION_FAILED";
constexpr const char* kErrLocalMirrorWriteFailed = "LOCAL_MIRROR_WRITE_FAILED";
constexpr const char* kErrRuntimeStatusReportFailed = "RUNTIME_STATUS_REPORT_FAILED";

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_STATUS_H

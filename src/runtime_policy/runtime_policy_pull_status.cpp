// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_policy_pull_status.h"

namespace bytetaper::runtime_policy {

const char* to_string(RuntimePolicyPullState state) {
    switch (state) {
    case RuntimePolicyPullState::Stopped:
        return "stopped";
    case RuntimePolicyPullState::Starting:
        return "starting";
    case RuntimePolicyPullState::Idle:
        return "idle";
    case RuntimePolicyPullState::CheckingControlPlane:
        return "checking_control_plane";
    case RuntimePolicyPullState::FetchingPolicy:
        return "fetching_policy";
    case RuntimePolicyPullState::ValidatingPolicy:
        return "validating_policy";
    case RuntimePolicyPullState::ActivatingPolicy:
        return "activating_policy";
    case RuntimePolicyPullState::Active:
        return "active";
    case RuntimePolicyPullState::DegradedControlPlaneUnavailable:
        return "degraded_control_plane_unavailable";
    case RuntimePolicyPullState::ActivationFailed:
        return "activation_failed";
    case RuntimePolicyPullState::PolicyInactive:
        return "policy_inactive";
    }
    return "unknown";
}

} // namespace bytetaper::runtime_policy

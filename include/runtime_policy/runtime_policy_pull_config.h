// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_CONFIG_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_CONFIG_H

#include "control_plane/policy_state_key.h"
#include "taperquery/policy_persistence.h"

#include <cstdint>
#include <string>

namespace bytetaper::runtime_policy {

enum class WhenControlPlaneUnavailable : std::uint8_t {
    ContinueLastKnownGood,
};

struct RuntimePolicyPullConfig {
    bool enabled = false;
    control_plane::PolicyResourceKey resource_key =
        control_plane::PolicyResourceKey::default_runtime();
    std::uint32_t pull_interval_ms = 5000;
    std::uint32_t startup_fetch_timeout_ms = 3000;
    std::uint32_t request_timeout_ms = 1000;
    std::uint32_t max_consecutive_failures = 12;

    taperquery::LocalPolicyPersistenceConfig local_mirror{};
    WhenControlPlaneUnavailable when_control_plane_unavailable =
        WhenControlPlaneUnavailable::ContinueLastKnownGood;

    std::string runtime_id = "bytetaper-runtime";
    std::string gateway_adapter = "envoy";
    std::string data_path_mode = "optimized";
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_PULL_CONFIG_H

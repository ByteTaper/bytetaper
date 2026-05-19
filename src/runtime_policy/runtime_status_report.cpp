// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_status_report.h"

#include <cstdio>

namespace bytetaper::runtime_policy {

std::string serialize_runtime_status_report(const RuntimePolicyStatusReport& report) {
    char buf[2048];
    std::snprintf(
        buf, sizeof(buf),
        R"({"runtimeId":"%s","resourceKey":"%s","activeGeneration":%llu,"activePolicyId":"%s","activeCanonicalHash":"%s","activationStatus":"%s","controlPlaneReachable":%s,"lastPullAtUnixEpochMs":%lld,"lastActivatedAtUnixEpochMs":%lld,"gatewayAdapter":"%s","dataPathMode":"%s"})",
        report.runtime_id.c_str(), report.resource_key.c_str(),
        static_cast<unsigned long long>(report.active_generation), report.active_policy_id.c_str(),
        report.active_canonical_hash.c_str(), report.activation_status.c_str(),
        report.control_plane_reachable ? "true" : "false",
        static_cast<long long>(report.last_pull_at_unix_epoch_ms),
        static_cast<long long>(report.last_activated_at_unix_epoch_ms),
        report.gateway_adapter.c_str(), report.data_path_mode.c_str());
    return std::string(buf);
}

} // namespace bytetaper::runtime_policy

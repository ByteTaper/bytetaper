// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/runtime_status_report.h"

#include "control_plane/policy_state_key.h"

namespace bytetaper::control_plane {

namespace {

bool is_non_empty(const std::string& value) {
    return !value.empty();
}

} // namespace

bool validate_runtime_status_report(const RuntimeStatusReport& report, std::string* error_out) {
    if (!is_non_empty(report.runtime_id)) {
        if (error_out != nullptr) {
            *error_out = "runtimeId is required";
        }
        return false;
    }
    if (!is_non_empty(report.resource_key)) {
        if (error_out != nullptr) {
            *error_out = "resourceKey is required";
        }
        return false;
    }
    PolicyResourceKey parsed{};
    if (!parse_resource_key(report.resource_key, &parsed)) {
        if (error_out != nullptr) {
            *error_out = "resourceKey is invalid";
        }
        return false;
    }
    if (!is_non_empty(report.gateway_adapter)) {
        if (error_out != nullptr) {
            *error_out = "gatewayAdapter is required";
        }
        return false;
    }
    if (!is_non_empty(report.active_policy_id)) {
        if (error_out != nullptr) {
            *error_out = "activePolicyId is required";
        }
        return false;
    }
    if (!is_non_empty(report.active_canonical_hash)) {
        if (error_out != nullptr) {
            *error_out = "activeCanonicalHash is required";
        }
        return false;
    }
    if (!is_non_empty(report.activation_status)) {
        if (error_out != nullptr) {
            *error_out = "activationStatus is required";
        }
        return false;
    }
    if (!is_non_empty(report.data_path_mode)) {
        if (error_out != nullptr) {
            *error_out = "dataPathMode is required";
        }
        return false;
    }
    if (report.last_pull_at_unix_epoch_ms <= 0) {
        if (error_out != nullptr) {
            *error_out = "lastPullAtUnixEpochMs is required";
        }
        return false;
    }
    return true;
}

} // namespace bytetaper::control_plane

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_RUNTIME_STATUS_REPORT_H
#define BYTETAPER_CONTROL_PLANE_RUNTIME_STATUS_REPORT_H

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

struct RuntimeStatusReport {
    std::string runtime_id;
    std::string resource_key;
    std::string gateway_adapter;
    std::uint64_t active_generation = 0;
    std::string active_policy_id;
    std::string active_canonical_hash;
    std::string activation_status;
    bool control_plane_reachable = false;
    std::string data_path_mode;
    std::int64_t last_pull_at_unix_epoch_ms = 0;
    std::int64_t last_activated_at_unix_epoch_ms = 0;
    std::string last_error_code;
    std::string last_error_message;
    std::int64_t received_at_unix_epoch_ms = 0;
};

struct RuntimeStatusReportResult {
    bool ok = false;
    std::string error;
    std::string error_code;
};

constexpr const char* kErrRuntimeStatusInvalid = "RUNTIME_STATUS_INVALID";
constexpr const char* kErrRuntimeStatusResourceUnknown = "RUNTIME_STATUS_RESOURCE_UNKNOWN";

bool validate_runtime_status_report(const RuntimeStatusReport& report, std::string* error_out);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_RUNTIME_STATUS_REPORT_H

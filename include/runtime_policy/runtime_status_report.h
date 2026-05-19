// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_STATUS_REPORT_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_STATUS_REPORT_H

#include <cstdint>
#include <string>

namespace bytetaper::runtime_policy {

struct RuntimePolicyStatusReport {
    std::string runtime_id;
    std::string resource_key;
    std::uint64_t active_generation = 0;
    std::string active_policy_id;
    std::string active_canonical_hash;
    std::string activation_status;
    bool control_plane_reachable = false;
    std::int64_t last_pull_at_unix_epoch_ms = 0;
    std::int64_t last_activated_at_unix_epoch_ms = 0;
    std::string gateway_adapter;
    std::string data_path_mode;
    std::string last_error_code;
    std::string last_error_message;
};

std::string serialize_runtime_status_report(const RuntimePolicyStatusReport& report);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_STATUS_REPORT_H

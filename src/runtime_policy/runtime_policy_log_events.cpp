// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_policy_log_events.h"

#include "control_plane/policy_lifecycle_event.h"
#include "observability/logger.h"

namespace bytetaper::runtime_policy {

namespace {

std::string escape_json(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

std::string
format_runtime_policy_lifecycle_log_json(const control_plane::PolicyLifecycleEvent& event) {
    std::string json = "{";
    json += "\"component\":\"runtime_policy\"";
    json += ",\"event\":\"";
    json += control_plane::to_string(event.event_type);
    json += "\"";
    if (!event.resource_key.empty()) {
        json += ",\"resourceKey\":\"";
        json += escape_json(event.resource_key);
        json += "\"";
    }
    if (event.after_generation != 0 || event.before_generation != 0) {
        json += ",\"generation\":";
        json += std::to_string(event.after_generation != 0 ? event.after_generation
                                                           : event.before_generation);
    }
    if (!event.after_policy_id.empty() || !event.policy_id.empty()) {
        json += ",\"policyId\":\"";
        json +=
            escape_json(event.after_policy_id.empty() ? event.policy_id : event.after_policy_id);
        json += "\"";
    }
    if (!event.stage.empty()) {
        json += ",\"failedStage\":\"";
        json += escape_json(event.stage);
        json += "\"";
    }
    if (!event.status.empty()) {
        json += ",\"status\":\"";
        json += escape_json(event.status);
        json += "\"";
    }
    if (!event.error_code.empty()) {
        json += ",\"errorCode\":\"";
        json += escape_json(event.error_code);
        json += "\"";
    }
    if (!event.message.empty()) {
        json += ",\"message\":\"";
        json += escape_json(event.message);
        json += "\"";
    }
    if (event.manual_resolution_required) {
        json += ",\"manualResolutionRequired\":true";
    }
    if (event.old_snapshot_still_active) {
        json += ",\"oldSnapshotStillActive\":true";
    }
    json += "}";
    return json;
}

void log_runtime_policy_lifecycle_event(const control_plane::PolicyLifecycleEvent& event) {
    const std::string json = format_runtime_policy_lifecycle_log_json(event);
    const bool is_error =
        event.status == "failure" || !event.error_code.empty() || event.manual_resolution_required;
    if (is_error) {
        bytetaper::observability::log_error(json.c_str());
    } else {
        bytetaper::observability::log_info(json.c_str());
    }
}

} // namespace bytetaper::runtime_policy

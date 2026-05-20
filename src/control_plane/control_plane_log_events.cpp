// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_log_events.h"

#include "control_plane/policy_lifecycle_event.h"
#include "observability/logger.h"

namespace bytetaper::control_plane {

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

void append_field(std::string* json, const char* key, const std::string& value, bool* first) {
    if (!value.empty()) {
        *json += *first ? "" : ",";
        *json += "\"";
        *json += key;
        *json += "\":\"";
        *json += escape_json(value);
        *json += "\"";
        *first = false;
    }
}

void append_uint_field(std::string* json, const char* key, std::uint64_t value, bool* first) {
    if (value != 0) {
        *json += *first ? "" : ",";
        *json += "\"";
        *json += key;
        *json += "\":";
        *json += std::to_string(value);
        *first = false;
    }
}

} // namespace

std::string format_control_plane_lifecycle_log_json(const PolicyLifecycleEvent& event) {
    std::string json = "{";
    bool first = true;
    json += "\"component\":\"control_plane\"";
    first = false;
    json += ",\"event\":\"";
    json += to_string(event.event_type);
    json += "\"";
    append_field(&json, "resourceKey", event.resource_key, &first);
    append_uint_field(
        &json, "generation",
        event.after_generation != 0 ? event.after_generation : event.before_generation, &first);
    append_field(&json, "policyId",
                 event.after_policy_id.empty() ? event.policy_id : event.after_policy_id, &first);
    append_field(&json, "jobId", event.job_id, &first);
    append_field(&json, "requestId", event.request_id, &first);
    append_field(&json, "stage", event.stage, &first);
    append_field(&json, "status", event.status, &first);
    append_field(&json, "errorCode", event.error_code, &first);
    append_field(&json, "message", event.message, &first);
    append_uint_field(&json, "durationMs", event.duration_ms, &first);
    append_uint_field(&json, "beforeGeneration", event.before_generation, &first);
    append_uint_field(&json, "afterGeneration", event.after_generation, &first);
    json += "}";
    return json;
}

void log_control_plane_lifecycle_event(const PolicyLifecycleEvent& event) {
    const std::string json = format_control_plane_lifecycle_log_json(event);
    const bool is_error = event.status == "failure" || !event.error_code.empty();
    if (is_error) {
        bytetaper::observability::log_error(json.c_str());
    } else {
        bytetaper::observability::log_info(json.c_str());
    }
}

} // namespace bytetaper::control_plane

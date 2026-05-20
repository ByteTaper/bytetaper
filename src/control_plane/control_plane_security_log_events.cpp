// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_security_log_events.h"

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

} // namespace

std::string format_control_plane_security_log_json(const ControlPlaneSecurityLogEvent& event) {
    std::string json = "{";
    bool first = true;
    json += "\"component\":\"control_plane_security\"";
    first = false;
    append_field(&json, "level", event.level, &first);
    append_field(&json, "event", event.event, &first);
    append_field(&json, "operation", event.operation, &first);
    append_field(&json, "resourceKey", event.resource_key, &first);
    append_field(&json, "remoteAddress", event.remote_address, &first);
    append_field(&json, "reason", event.reason, &first);
    append_field(&json, "deploymentMode", event.deployment_mode, &first);
    append_field(&json, "message", event.message, &first);
    json += "}";
    return json;
}

void log_control_plane_security_event(const ControlPlaneSecurityLogEvent& event) {
    const std::string json = format_control_plane_security_log_json(event);
    if (event.level == "ERROR") {
        bytetaper::observability::log_error(json.c_str());
    } else {
        bytetaper::observability::log_warn(json.c_str());
    }
}

} // namespace bytetaper::control_plane

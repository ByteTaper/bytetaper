// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SECURITY_LOG_EVENTS_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SECURITY_LOG_EVENTS_H

#include <string>

namespace bytetaper::control_plane {

struct ControlPlaneSecurityLogEvent {
    std::string level = "WARN";
    std::string event;
    std::string operation;
    std::string resource_key;
    std::string remote_address;
    std::string reason;
    std::string deployment_mode;
    std::string message;
};

std::string format_control_plane_security_log_json(const ControlPlaneSecurityLogEvent& event);

void log_control_plane_security_event(const ControlPlaneSecurityLogEvent& event);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SECURITY_LOG_EVENTS_H

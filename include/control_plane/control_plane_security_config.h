// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SECURITY_CONFIG_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SECURITY_CONFIG_H

#include "control_plane/control_plane_auth_provider.h"
#include "control_plane/control_plane_deployment_mode.h"

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

enum class RuntimeProcessRole : std::uint8_t {
    ControlPlane,
    RuntimeOnly,
    SingleNode,
};

const char* to_string(RuntimeProcessRole role);

RuntimeProcessRole parse_runtime_process_role(const std::string& value);

struct ControlPlaneSecurityConfig {
    bool enabled = true;
    bool enabled_explicit = false;
    bool runtime_role_explicit = false;
    ControlPlaneDeploymentMode deployment_mode = ControlPlaneDeploymentMode::LocalDev;
    RuntimeProcessRole runtime_role = RuntimeProcessRole::SingleNode;
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 19090;
    bool allow_public_bind = false;
    std::string control_plane_endpoint;
    ControlPlaneAuthConfig auth{};
};

ControlPlaneSecurityConfig load_control_plane_security_from_env(const char* admin_bind_address);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SECURITY_CONFIG_H

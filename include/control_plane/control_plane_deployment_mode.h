// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_DEPLOYMENT_MODE_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_DEPLOYMENT_MODE_H

#include <string>

namespace bytetaper::control_plane {

enum class ControlPlaneDeploymentMode {
    LocalDev,
    SingleNode,
    MultiRuntime,
    Production,
};

const char* to_string(ControlPlaneDeploymentMode mode);

ControlPlaneDeploymentMode parse_control_plane_deployment_mode(const std::string& value);

bool is_production_deployment_mode(ControlPlaneDeploymentMode mode);

// Production and multi-runtime deployments fail process startup on validate_startup errors.
bool should_fail_startup_on_validation_errors(ControlPlaneDeploymentMode mode);

// Runtime-only pods in these modes must configure control_plane_endpoint.
bool runtime_only_requires_control_plane_endpoint(ControlPlaneDeploymentMode mode);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_DEPLOYMENT_MODE_H

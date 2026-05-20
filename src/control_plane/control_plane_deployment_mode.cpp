// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_deployment_mode.h"

namespace bytetaper::control_plane {

const char* to_string(ControlPlaneDeploymentMode mode) {
    switch (mode) {
    case ControlPlaneDeploymentMode::LocalDev:
        return "local-dev";
    case ControlPlaneDeploymentMode::SingleNode:
        return "single-node";
    case ControlPlaneDeploymentMode::MultiRuntime:
        return "multi-runtime";
    case ControlPlaneDeploymentMode::Production:
        return "production";
    }
    return "local-dev";
}

ControlPlaneDeploymentMode parse_control_plane_deployment_mode(const std::string& value) {
    if (value == "single-node" || value == "single_node") {
        return ControlPlaneDeploymentMode::SingleNode;
    }
    if (value == "multi-runtime" || value == "multi_runtime") {
        return ControlPlaneDeploymentMode::MultiRuntime;
    }
    if (value == "production") {
        return ControlPlaneDeploymentMode::Production;
    }
    return ControlPlaneDeploymentMode::LocalDev;
}

bool is_production_deployment_mode(ControlPlaneDeploymentMode mode) {
    return mode == ControlPlaneDeploymentMode::Production;
}

bool should_fail_startup_on_validation_errors(ControlPlaneDeploymentMode mode) {
    return mode == ControlPlaneDeploymentMode::Production ||
           mode == ControlPlaneDeploymentMode::MultiRuntime;
}

bool runtime_only_requires_control_plane_endpoint(ControlPlaneDeploymentMode mode) {
    return should_fail_startup_on_validation_errors(mode);
}

} // namespace bytetaper::control_plane

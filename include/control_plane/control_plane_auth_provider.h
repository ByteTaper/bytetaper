// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_AUTH_PROVIDER_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_AUTH_PROVIDER_H

#include "control_plane/control_plane_deployment_mode.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace bytetaper::control_plane {

enum class AuthDecision : std::uint8_t {
    Allow,
    Deny,
    Unauthenticated,
    Unsupported,
};

enum class ControlPlaneAuthMode : std::uint8_t {
    None,
    StaticToken,
    Mtls,
    Oidc,
    External,
};

struct ControlPlaneAuthConfig {
    ControlPlaneAuthMode mode = ControlPlaneAuthMode::None;
    std::string token_env = "BYTETAPER_CONTROL_PLANE_TOKEN";
    bool allow_unauthenticated_production_mutation = false;
};

struct ControlPlaneAuthContext {
    std::string path;
    std::string method;
    std::string operation;
    std::string resource_key;
    std::string operator_id;
    std::string remote_address;
    std::unordered_map<std::string, std::string> headers;
    ControlPlaneDeploymentMode deployment_mode = ControlPlaneDeploymentMode::LocalDev;
    bool internal_call = false;
    bool mutation = true;
};

class ControlPlaneAuthProvider {
public:
    virtual ~ControlPlaneAuthProvider() = default;

    virtual AuthDecision authorize(const ControlPlaneAuthContext& context) = 0;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_AUTH_PROVIDER_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_security_config.h"

#include "control_plane/control_plane_guardrails.h"

#include <cstdlib>
#include <cstring>

namespace bytetaper::control_plane {

namespace {

bool env_is_true(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "yes") == 0 || std::strcmp(value, "ON") == 0;
}

} // namespace

ControlPlaneSecurityConfig load_control_plane_security_from_env(const char* admin_bind_address) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;

    if (const char* enabled = std::getenv("BYTETAPER_CONTROL_PLANE_ENABLED"); enabled != nullptr) {
        security.enabled = env_is_true(enabled);
        security.enabled_explicit = true;
    }

    if (const char* mode = std::getenv("BYTETAPER_DEPLOYMENT_MODE"); mode != nullptr) {
        security.deployment_mode = parse_control_plane_deployment_mode(mode);
    }

    if (const char* role = std::getenv("BYTETAPER_RUNTIME_ROLE"); role != nullptr) {
        security.runtime_role = parse_runtime_process_role(role);
        security.runtime_role_explicit = true;
    }

    if (const char* bind = std::getenv("BYTETAPER_CONTROL_PLANE_BIND_ADDRESS"); bind != nullptr) {
        security.bind_address = bind;
    } else if (admin_bind_address != nullptr && admin_bind_address[0] != '\0') {
        security.bind_address = admin_bind_address;
    }

    if (const char* allow_public = std::getenv("BYTETAPER_CONTROL_PLANE_ALLOW_PUBLIC_BIND");
        allow_public != nullptr) {
        security.allow_public_bind = env_is_true(allow_public);
    }

    if (const char* endpoint = std::getenv("BYTETAPER_CONTROL_PLANE_ENDPOINT");
        endpoint != nullptr) {
        security.control_plane_endpoint = endpoint;
    }

    if (const char* auth_mode = std::getenv("BYTETAPER_CONTROL_PLANE_AUTH_MODE");
        auth_mode != nullptr) {
        security.auth.mode = parse_control_plane_auth_mode(auth_mode);
    }

    if (const char* token_env = std::getenv("BYTETAPER_CONTROL_PLANE_TOKEN_ENV");
        token_env != nullptr) {
        security.auth.token_env = token_env;
    }

    if (const char* unsafe =
            std::getenv("BYTETAPER_CONTROL_PLANE_ALLOW_UNAUTHENTICATED_PRODUCTION");
        unsafe != nullptr) {
        security.auth.allow_unauthenticated_production_mutation = env_is_true(unsafe);
    }

    if (const char* port = std::getenv("BYTETAPER_CONTROL_PLANE_PORT"); port != nullptr) {
        security.port = static_cast<std::uint16_t>(std::strtoul(port, nullptr, 10));
    }

    return security;
}

const char* to_string(RuntimeProcessRole role) {
    switch (role) {
    case RuntimeProcessRole::ControlPlane:
        return "control-plane";
    case RuntimeProcessRole::RuntimeOnly:
        return "runtime-only";
    case RuntimeProcessRole::SingleNode:
        return "single-node";
    }
    return "single-node";
}

RuntimeProcessRole parse_runtime_process_role(const std::string& value) {
    if (value == "control-plane" || value == "control_plane") {
        return RuntimeProcessRole::ControlPlane;
    }
    if (value == "runtime-only" || value == "runtime_only") {
        return RuntimeProcessRole::RuntimeOnly;
    }
    return RuntimeProcessRole::SingleNode;
}

} // namespace bytetaper::control_plane

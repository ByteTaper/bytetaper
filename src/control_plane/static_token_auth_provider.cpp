// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/static_token_auth_provider.h"

#include <cstdlib>
#include <cstring>
#include <memory>

namespace bytetaper::control_plane {

namespace {

constexpr const char* kBearerPrefix = "Bearer ";

bool constant_time_equals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string read_token_from_env(const std::string& env_name) {
    if (env_name.empty()) {
        return {};
    }
    const char* value = std::getenv(env_name.c_str());
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return std::string(value);
}

class ReservedAuthProvider : public ControlPlaneAuthProvider {
public:
    AuthDecision authorize(const ControlPlaneAuthContext&) override {
        return AuthDecision::Unsupported;
    }
};

} // namespace

StaticTokenAuthProvider::StaticTokenAuthProvider(ControlPlaneAuthConfig config,
                                                 std::string configured_token)
    : config_(std::move(config)), configured_token_(std::move(configured_token)) {
    if (configured_token_.empty()) {
        configured_token_ = read_token_from_env(config_.token_env);
    }
}

std::string
StaticTokenAuthProvider::extract_bearer_token(const ControlPlaneAuthContext& context) const {
    const auto auth_it = context.headers.find("authorization");
    if (auth_it == context.headers.end()) {
        const auto alt_it = context.headers.find("Authorization");
        if (alt_it == context.headers.end()) {
            return {};
        }
        const std::string& header = alt_it->second;
        if (header.size() > std::strlen(kBearerPrefix) &&
            header.compare(0, std::strlen(kBearerPrefix), kBearerPrefix) == 0) {
            return header.substr(std::strlen(kBearerPrefix));
        }
        return header;
    }
    const std::string& header = auth_it->second;
    if (header.size() > std::strlen(kBearerPrefix) &&
        header.compare(0, std::strlen(kBearerPrefix), kBearerPrefix) == 0) {
        return header.substr(std::strlen(kBearerPrefix));
    }
    return header;
}

AuthDecision StaticTokenAuthProvider::authorize(const ControlPlaneAuthContext& context) {
    if (context.internal_call) {
        return AuthDecision::Allow;
    }
    if (!context.mutation) {
        return AuthDecision::Allow;
    }

    const std::string presented = extract_bearer_token(context);
    if (configured_token_.empty()) {
        if (context.deployment_mode == ControlPlaneDeploymentMode::LocalDev) {
            return AuthDecision::Allow;
        }
        return AuthDecision::Unauthenticated;
    }
    if (presented.empty()) {
        return AuthDecision::Unauthenticated;
    }
    if (!constant_time_equals(presented, configured_token_)) {
        return AuthDecision::Deny;
    }
    return AuthDecision::Allow;
}

std::unique_ptr<ControlPlaneAuthProvider> make_auth_provider(const ControlPlaneAuthConfig& config,
                                                             ControlPlaneDeploymentMode mode) {
    (void) mode;
    switch (config.mode) {
    case ControlPlaneAuthMode::None:
        return nullptr;
    case ControlPlaneAuthMode::StaticToken:
        return std::make_unique<StaticTokenAuthProvider>(config, std::string{});
    case ControlPlaneAuthMode::Mtls:
    case ControlPlaneAuthMode::Oidc:
    case ControlPlaneAuthMode::External:
        return std::make_unique<ReservedAuthProvider>();
    }
    return nullptr;
}

} // namespace bytetaper::control_plane

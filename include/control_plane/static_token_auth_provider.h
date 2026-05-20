// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_STATIC_TOKEN_AUTH_PROVIDER_H
#define BYTETAPER_CONTROL_PLANE_STATIC_TOKEN_AUTH_PROVIDER_H

#include "control_plane/control_plane_auth_provider.h"

#include <memory>
#include <string>

namespace bytetaper::control_plane {

class StaticTokenAuthProvider : public ControlPlaneAuthProvider {
public:
    explicit StaticTokenAuthProvider(ControlPlaneAuthConfig config, std::string configured_token);

    AuthDecision authorize(const ControlPlaneAuthContext& context) override;

private:
    std::string extract_bearer_token(const ControlPlaneAuthContext& context) const;

    ControlPlaneAuthConfig config_;
    std::string configured_token_;
};

std::unique_ptr<ControlPlaneAuthProvider> make_auth_provider(const ControlPlaneAuthConfig& config,
                                                             ControlPlaneDeploymentMode mode);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_STATIC_TOKEN_AUTH_PROVIDER_H

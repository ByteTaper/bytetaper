// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"

#include <functional>

namespace bytetaper::control_plane {

struct ControlPlaneServiceConfig {
    PolicyStateStore* policy_state_store = nullptr;

    // Optional hook for runtime policy inactive state (BT-CP-013 will replace this).
    std::function<bool(const PolicyResourceKey&)> is_policy_inactive;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H

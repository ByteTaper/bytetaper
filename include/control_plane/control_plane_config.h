// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H

#include "control_plane/fleet_status_config.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"

#include <functional>

namespace bytetaper::control_plane {

class PolicyUpdateQueue;

struct ControlPlaneServiceConfig {
    PolicyStateStore* policy_state_store = nullptr;
    PolicyUpdateQueue* policy_update_queue = nullptr;
    FleetStatusConfig fleet_status{};

    std::function<bool(const PolicyResourceKey&)> is_policy_inactive;

    std::function<RepairLocalHookResult(const PolicyRepairLocalRequest& request,
                                        const ActivePolicyPointer& committed)>
        repair_local_hook;

    AdoptLocalFetchHook adopt_local_fetch_hook;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_REPAIR_OPERATION_H
#define BYTETAPER_CONTROL_PLANE_POLICY_REPAIR_OPERATION_H

#include "control_plane/control_plane_config.h"
#include "control_plane/fleet_status_service.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_state_store.h"

namespace bytetaper::control_plane {

class PolicyRepairOperation {
public:
    PolicyRepairOperation(ControlPlaneServiceConfig config, PolicyStateStore* store,
                          FleetStatusService* fleet_service);

    PolicyRepairLocalPlanResult plan(const PolicyRepairLocalPlanRequest& request);
    PolicyRepairLocalResult execute(const PolicyRepairLocalRequest& request);
    PolicyRepairLocalResult run_repair_hook(const PolicyRepairLocalRequest& request);

private:
    ControlPlaneServiceConfig config_;
    PolicyStateStore* store_;
    FleetStatusService* fleet_service_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_REPAIR_OPERATION_H

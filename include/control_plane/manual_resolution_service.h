// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_SERVICE_H
#define BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_SERVICE_H

#include "control_plane/control_plane_config.h"
#include "control_plane/fleet_status_service.h"
#include "control_plane/manual_resolution_api.h"

namespace bytetaper::control_plane {

class ManualResolutionService {
public:
    ManualResolutionService(ControlPlaneServiceConfig config, FleetStatusService* fleet_service);

    PolicyRepairLocalPlanResult plan_repair_local(const PolicyRepairLocalPlanRequest& request);
    PolicyRepairLocalResult repair_local(const PolicyRepairLocalRequest& request);

    PolicyAdoptLocalPlanResult plan_adopt_local(const PolicyAdoptLocalPlanRequest& request);
    PolicyAdoptLocalResult adopt_local(const PolicyAdoptLocalRequest& request);

    PolicyRollbackResult rollback(const PolicyRollbackRequest& request);

private:
    bool record_manual_audit(const PolicyResourceKey& key, const PolicyAuditRecord& record);

    ControlPlaneServiceConfig config_;
    FleetStatusService* fleet_service_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_SERVICE_H

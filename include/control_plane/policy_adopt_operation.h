// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_ADOPT_OPERATION_H
#define BYTETAPER_CONTROL_PLANE_POLICY_ADOPT_OPERATION_H

#include "control_plane/control_plane_config.h"
#include "control_plane/fleet_status_service.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_state_store.h"

namespace bytetaper::control_plane {

class PolicyAdoptOperation {
public:
    PolicyAdoptOperation(ControlPlaneServiceConfig config, PolicyStateStore* store,
                         FleetStatusService* fleet_service);

    PolicyAdoptLocalPlanResult plan(const PolicyAdoptLocalPlanRequest& request);
    PolicyAdoptLocalResult execute(const PolicyAdoptLocalRequest& request);

private:
    struct ResolvedLocalPolicy {
        bool ok = false;
        PolicyApplyStatus status = PolicyApplyStatus::InternalError;
        std::string error;
        std::string error_code;
        std::string canonical_yaml;
        std::uint64_t local_generation_hint = 0;
        std::uint64_t resolved_local_generation = 0;
        std::string local_policy_id;
        std::string local_canonical_hash;
    };

    ResolvedLocalPolicy fetch_local_policy(const PolicyAdoptLocalRequest& request);

    ControlPlaneServiceConfig config_;
    PolicyStateStore* store_;
    FleetStatusService* fleet_service_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_ADOPT_OPERATION_H

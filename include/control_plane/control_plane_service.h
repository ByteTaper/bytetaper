// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SERVICE_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SERVICE_H

#include "control_plane/control_plane_config.h"
#include "control_plane/fleet_status.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_apply_api.h"
#include "control_plane/policy_job_query.h"
#include "control_plane/policy_version_query.h"
#include "control_plane/runtime_status_report.h"

#include <memory>

namespace bytetaper::control_plane {

class ControlPlaneService {
public:
    explicit ControlPlaneService(ControlPlaneServiceConfig config);
    ~ControlPlaneService();

    PolicyDryRunResult dry_run(const PolicyDryRunRequest& request);

    PolicyApplySubmitResult apply(const PolicyApplyRequest& request);

    PolicyDiffResult diff(const PolicyDiffRequest& request);

    PolicyRollbackPlanResult plan_rollback(const PolicyRollbackPlanRequest& request);

    ActivePolicyResult get_active_policy(const PolicyResourceKey& resource_key);

    PolicyVersionFetchResult get_policy_version(const PolicyResourceKey& resource_key,
                                                std::uint64_t generation);

    PolicyVersionListResult list_policy_versions(const PolicyResourceKey& resource_key);

    PolicyJobQueryResult get_policy_update_job(const std::string& job_id,
                                               const PolicyResourceKey& resource_key);

    RuntimeStatusReportResult report_runtime_status(const RuntimeStatusReport& report);

    FleetStatusResult get_fleet_status(const PolicyResourceKey& resource_key);

    PolicyRepairLocalPlanResult plan_repair_local(const PolicyRepairLocalPlanRequest& request);

    PolicyRepairLocalResult repair_local(const PolicyRepairLocalRequest& request);

    PolicyAdoptLocalPlanResult plan_adopt_local(const PolicyAdoptLocalPlanRequest& request);

    PolicyAdoptLocalResult adopt_local(const PolicyAdoptLocalRequest& request);

    PolicyRollbackResult rollback(const PolicyRollbackRequest& request);

private:
    ControlPlaneServiceConfig config_;
    std::unique_ptr<class FleetStatusService> fleet_status_service_;
    std::unique_ptr<class ManualResolutionService> manual_resolution_service_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SERVICE_H

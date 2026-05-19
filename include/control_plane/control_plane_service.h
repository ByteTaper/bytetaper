// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SERVICE_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SERVICE_H

#include "control_plane/control_plane_config.h"
#include "control_plane/policy_apply_api.h"
#include "control_plane/policy_version_query.h"

namespace bytetaper::control_plane {

class ControlPlaneService {
public:
    explicit ControlPlaneService(ControlPlaneServiceConfig config);

    PolicyDryRunResult dry_run(const PolicyDryRunRequest& request);

    PolicyApplySubmitResult apply(const PolicyApplyRequest& request);

    PolicyDiffResult diff(const PolicyDiffRequest& request);

    PolicyRollbackPlanResult plan_rollback(const PolicyRollbackPlanRequest& request);

    ActivePolicyResult get_active_policy(const PolicyResourceKey& resource_key);

    PolicyVersionListResult list_policy_versions(const PolicyResourceKey& resource_key);

private:
    ControlPlaneServiceConfig config_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_SERVICE_H

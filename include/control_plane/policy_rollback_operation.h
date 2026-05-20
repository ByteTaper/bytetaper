// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_ROLLBACK_OPERATION_H
#define BYTETAPER_CONTROL_PLANE_POLICY_ROLLBACK_OPERATION_H

#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_state_store.h"

namespace bytetaper::control_plane {

class PolicyRollbackOperation {
public:
    explicit PolicyRollbackOperation(PolicyStateStore* store);

    PolicyRollbackResult execute(const PolicyRollbackRequest& request);

private:
    PolicyStateStore* store_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_ROLLBACK_OPERATION_H

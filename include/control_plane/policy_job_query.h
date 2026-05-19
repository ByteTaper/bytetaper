// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_JOB_QUERY_H
#define BYTETAPER_CONTROL_PLANE_POLICY_JOB_QUERY_H

#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_state_record.h"

#include <string>

namespace bytetaper::control_plane {

struct PolicyJobQueryResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    PolicyUpdateJobRecord job;
    std::string error;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_JOB_QUERY_H

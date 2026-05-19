// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_JOB_RECORD_H
#define BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_JOB_RECORD_H

#include "control_plane/policy_state_record.h"
#include "control_plane/policy_update_job.h"

namespace bytetaper::control_plane {

PolicyUpdateJobRecord to_job_record(const PolicyUpdateJob& job);

void apply_job_record_to_job(const PolicyUpdateJobRecord& record, PolicyUpdateJob* job);

PolicyUpdateJobState parse_policy_update_job_state(const std::string& state);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_JOB_RECORD_H

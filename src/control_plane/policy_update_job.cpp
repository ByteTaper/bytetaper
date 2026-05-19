// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_update_job.h"

namespace bytetaper::control_plane {

const char* to_string(PolicyUpdateJobState state) {
    switch (state) {
    case PolicyUpdateJobState::Submitted:
        return "submitted";
    case PolicyUpdateJobState::Queued:
        return "queued";
    case PolicyUpdateJobState::Processing:
        return "processing";
    case PolicyUpdateJobState::CandidateBuilt:
        return "candidate_built";
    case PolicyUpdateJobState::VersionStored:
        return "version_stored";
    case PolicyUpdateJobState::ActivePromoted:
        return "active_promoted";
    case PolicyUpdateJobState::Committed:
        return "committed";
    case PolicyUpdateJobState::Failed:
        return "failed";
    case PolicyUpdateJobState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

} // namespace bytetaper::control_plane

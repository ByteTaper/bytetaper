// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_JOB_H
#define BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_JOB_H

#include "taperquery/tq_apply_service.h"

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

enum class PolicyUpdateJobState : std::uint8_t {
    Submitted,
    Queued,
    Processing,
    CandidateBuilt,
    VersionStored,
    ActivePromoted,
    Committed,
    Failed,
    Cancelled,
};

struct PolicyUpdateJobFailure {
    std::string stage;
    std::string code;
    std::string message;
    std::uint64_t expected_generation = 0;
    std::uint64_t actual_generation = 0;
};

struct PolicyUpdateJob {
    std::string job_id;
    std::string resource_key;
    std::uint32_t logical_shard_id = 0;
    PolicyUpdateJobState state = PolicyUpdateJobState::Submitted;
    std::string source_type;
    std::string operator_id;
    std::string request_id;
    std::uint64_t expected_base_generation = 0;
    std::string expected_base_policy_id;
    std::uint64_t candidate_generation = 0;
    std::string candidate_policy_id;
    std::string candidate_canonical_hash;
    std::uint64_t submitted_at_unix_epoch_ms = 0;
    std::uint64_t updated_at_unix_epoch_ms = 0;
    PolicyUpdateJobFailure failure{};
    taperquery::TqApplyRequest apply_request{};
};

const char* to_string(PolicyUpdateJobState state);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_JOB_H

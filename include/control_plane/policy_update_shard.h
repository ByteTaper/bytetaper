// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_SHARD_H
#define BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_SHARD_H

#include "control_plane/policy_update_job.h"

#include <cstdint>
#include <deque>
#include <mutex>

namespace bytetaper::control_plane {

enum class PolicyUpdateShardState : std::uint8_t {
    Idle,
    Queued,
    Processing,
    Draining,
    Stopped,
};

const char* to_string(PolicyUpdateShardState state);

struct PolicyUpdateShard {
    std::uint32_t shard_id = 0;
    PolicyUpdateShardState state = PolicyUpdateShardState::Idle;
    std::deque<PolicyUpdateJob> jobs;
    std::mutex mu;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_UPDATE_SHARD_H

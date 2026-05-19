// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_update_shard.h"

namespace bytetaper::control_plane {

const char* to_string(PolicyUpdateShardState state) {
    switch (state) {
    case PolicyUpdateShardState::Idle:
        return "idle";
    case PolicyUpdateShardState::Queued:
        return "queued";
    case PolicyUpdateShardState::Processing:
        return "processing";
    case PolicyUpdateShardState::Draining:
        return "draining";
    case PolicyUpdateShardState::Stopped:
        return "stopped";
    }
    return "unknown";
}

} // namespace bytetaper::control_plane

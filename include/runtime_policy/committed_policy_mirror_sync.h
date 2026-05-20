// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_COMMITTED_POLICY_MIRROR_SYNC_H
#define BYTETAPER_RUNTIME_POLICY_COMMITTED_POLICY_MIRROR_SYNC_H

#include "control_plane/policy_state_key.h"
#include "runtime_policy/control_plane_policy_client.h"
#include "taperquery/policy_persistence.h"

#include <cstdint>
#include <string>

namespace bytetaper::runtime_policy {

struct CommittedPolicyMirrorSyncResult {
    bool ok = false;
    std::string error;
    std::string error_code;
};

CommittedPolicyMirrorSyncResult sync_local_mirror_from_committed(
    ControlPlanePolicyClient* client, const control_plane::PolicyResourceKey& resource_key,
    const taperquery::LocalPolicyPersistenceConfig& mirror_config, std::uint64_t generation);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_COMMITTED_POLICY_MIRROR_SYNC_H

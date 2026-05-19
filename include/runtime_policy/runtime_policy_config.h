// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_CONFIG_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_CONFIG_H

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/policy_inactive_mode.h"
#include "taperquery/policy_persistence.h"

namespace bytetaper::runtime_policy {

struct RuntimePolicyPlaneConfig {
    const char* bootstrap_policy_file = nullptr;
    taperquery::LocalPolicyPersistenceConfig persistence_config{};
    runtime::RuntimePolicyStore* runtime_policy_store = nullptr;
    control_plane::PolicyStateStore* policy_state_store = nullptr;
    control_plane::PolicyResourceKey resource_key =
        control_plane::PolicyResourceKey::default_runtime();
    PolicyInactiveConfig inactive_config{};
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_CONFIG_H

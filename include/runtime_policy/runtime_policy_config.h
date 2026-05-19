// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_CONFIG_H
#define BYTETAPER_RUNTIME_POLICY_CONFIG_H

#include "runtime/policy_snapshot.h"
#include "taperquery/policy_persistence.h"

namespace bytetaper::runtime_policy {

// Configuration for the RuntimePolicyPlane. Carries everything needed to load
// and install the initial policy at startup — no Envoy types or extproc headers.
struct RuntimePolicyPlaneConfig {
    const char* bootstrap_policy_file = nullptr; // may be null
    taperquery::LocalPolicyPersistenceConfig persistence_config{};
    runtime::RuntimePolicyStore* runtime_policy_store = nullptr;
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_CONFIG_H

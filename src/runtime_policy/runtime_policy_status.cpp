// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_policy_status.h"

namespace bytetaper::runtime_policy {

RuntimePolicyStatusCode status_code_for_health(RuntimePolicyHealth health) {
    switch (health) {
    case RuntimePolicyHealth::Active:
        return RuntimePolicyStatusCode::LoadedFromPersistedPolicy;
    case RuntimePolicyHealth::BootstrapImported:
        return RuntimePolicyStatusCode::BootstrapImported;
    case RuntimePolicyHealth::BootstrapDriftDetected:
        return RuntimePolicyStatusCode::BootstrapDriftDetected;
    case RuntimePolicyHealth::NoPolicyConfigured:
        return RuntimePolicyStatusCode::NoPolicyConfigured;
    case RuntimePolicyHealth::StartupFailed:
        return RuntimePolicyStatusCode::StartupFailed;
    case RuntimePolicyHealth::InactiveMissingLocal:
    case RuntimePolicyHealth::InactiveStaleLocal:
    case RuntimePolicyHealth::InactiveDivergedLocal:
    case RuntimePolicyHealth::InactiveCorruptedLocal:
    case RuntimePolicyHealth::InactiveUnparseableLocal:
    case RuntimePolicyHealth::InactiveActivePointerMissingTarget:
    case RuntimePolicyHealth::InactiveMetadataMissing:
    case RuntimePolicyHealth::InactiveMetadataInvalid:
        return RuntimePolicyStatusCode::PolicyInactive;
    case RuntimePolicyHealth::Unknown:
    default:
        return RuntimePolicyStatusCode::Unknown;
    }
}

} // namespace bytetaper::runtime_policy

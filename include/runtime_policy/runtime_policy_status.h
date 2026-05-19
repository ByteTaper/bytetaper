// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_STATUS_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_STATUS_H

#include <string>

namespace bytetaper::runtime_policy {

enum class RuntimePolicyHealth {
    Unknown,
    Active,
    BootstrapImported,
    BootstrapDriftDetected,
    InactiveMissingLocal,
    InactiveStaleLocal,
    InactiveDivergedLocal,
    InactiveCorruptedLocal,
    InactiveUnparseableLocal,
    InactiveActivePointerMissingTarget,
    InactiveMetadataMissing,
    InactiveMetadataInvalid,
    StartupFailed,
    NoPolicyConfigured,
};

enum class RuntimePolicyStatusCode {
    Unknown,
    LoadedFromPersistedPolicy,
    LoadedFromBootstrapPolicy,
    LoadedEmptyPolicy,
    StartupFailed,
    PolicyInactive,
    BootstrapDriftDetected,
    BootstrapImported,
    NoPolicyConfigured,
};

struct RuntimePolicyStatus {
    RuntimePolicyStatusCode code = RuntimePolicyStatusCode::Unknown;
    RuntimePolicyHealth health = RuntimePolicyHealth::Unknown;
    std::string error_message;
};

RuntimePolicyStatusCode status_code_for_health(RuntimePolicyHealth health);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_STATUS_H

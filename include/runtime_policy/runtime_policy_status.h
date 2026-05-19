// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_STATUS_H
#define BYTETAPER_RUNTIME_POLICY_STATUS_H

#include <string>

namespace bytetaper::runtime_policy {

enum class RuntimePolicyStatusCode {
    Unknown,
    LoadedFromPersistedPolicy,
    LoadedFromBootstrapPolicy,
    LoadedEmptyPolicy,
    StartupFailed,
    // Reserved for future: PolicyInactive*, PolicyActivationFailed, PolicyActive
};

struct RuntimePolicyStatus {
    RuntimePolicyStatusCode code = RuntimePolicyStatusCode::Unknown;
    std::string error_message;
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_STATUS_H

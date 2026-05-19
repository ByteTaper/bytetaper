// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_POLICY_INACTIVE_MODE_H
#define BYTETAPER_RUNTIME_POLICY_POLICY_INACTIVE_MODE_H

namespace bytetaper::runtime_policy {

enum class PolicyInactiveMode {
    PassThrough, // data path continues without transform/optimization
    Reject,      // data path rejects requests
    StartupFail, // process exits / start() returns !ok
};

struct PolicyInactiveConfig {
    PolicyInactiveMode when_inactive = PolicyInactiveMode::PassThrough;
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_POLICY_INACTIVE_MODE_H

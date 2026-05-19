// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_STARTUP_POLICY_LOADER_H
#define BYTETAPER_RUNTIME_POLICY_STARTUP_POLICY_LOADER_H

#include "runtime/policy_snapshot.h"
#include "runtime_policy/runtime_policy_config.h"
#include "runtime_policy/runtime_policy_diagnostics.h"
#include "runtime_policy/runtime_policy_status.h"

#include <memory>

namespace bytetaper::runtime_policy {

// Result of the startup policy load sequence.
struct StartupPolicyLoadResult {
    bool ok = false;
    RuntimePolicyStatusCode status_code = RuntimePolicyStatusCode::Unknown;
    std::shared_ptr<const runtime::RuntimePolicySnapshot> snapshot;
    RuntimePolicyDiagnostics diagnostics;
};

// Encapsulates the startup source-selection logic extracted from server_main.cpp.
// Priority order:
//   1. Persisted policy (if persistence enabled and files present and valid)
//   2. Bootstrap policy file (if configured)
//   3. Empty policy (no routes)
//
// Corruption of a persisted policy results in StartupFailed — no silent fallback.
class StartupPolicyLoader {
public:
    explicit StartupPolicyLoader(const RuntimePolicyPlaneConfig& config);

    StartupPolicyLoadResult load(std::uint64_t next_generation);

private:
    StartupPolicyLoadResult try_load_persisted();
    StartupPolicyLoadResult load_bootstrap();
    StartupPolicyLoadResult load_empty();

    const RuntimePolicyPlaneConfig& config_;
    std::uint64_t next_generation_ = 1;
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_STARTUP_POLICY_LOADER_H

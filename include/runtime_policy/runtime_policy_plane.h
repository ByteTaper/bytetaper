// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_PLANE_H
#define BYTETAPER_RUNTIME_POLICY_PLANE_H

#include "runtime_policy/runtime_policy_config.h"
#include "runtime_policy/runtime_policy_diagnostics.h"
#include "runtime_policy/runtime_policy_pull_loop.h"
#include "runtime_policy/runtime_policy_status.h"

#include <memory>

namespace bytetaper::runtime_policy {

// Result returned from RuntimePolicyPlane::start().
struct RuntimePolicyPlaneStartResult {
    bool ok = false;
    RuntimePolicyStatus status;
    RuntimePolicyDiagnostics diagnostics;
};

// Owns the startup lifecycle of the Runtime Policy Plane.
// Delegates source-selection to StartupPolicyLoader, then installs the snapshot
// into the RuntimePolicyStore. Does not depend on any gateway adapter or Envoy type.
class RuntimePolicyPlane {
public:
    explicit RuntimePolicyPlane(RuntimePolicyPlaneConfig config);

    // Runs the full startup sequence: load → install_initial.
    // Populates internal status and diagnostics regardless of outcome.
    RuntimePolicyPlaneStartResult start();

    const RuntimePolicyStatus& status() const;
    const RuntimePolicyDiagnostics& diagnostics() const;

    bool start_pull_loop(const RuntimePolicyPullLoopConfig& pull_config);
    void stop_pull_loop();
    RuntimePolicyPullStatus pull_status() const;

private:
    RuntimePolicyPlaneConfig config_;
    RuntimePolicyStatus status_;
    RuntimePolicyDiagnostics diagnostics_;
    std::unique_ptr<RuntimePolicyPullLoop> pull_loop_;
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_PLANE_H

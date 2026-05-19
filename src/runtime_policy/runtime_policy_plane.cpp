// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/runtime_policy_plane.h"

#include "observability/logger.h"
#include "runtime_policy/startup_policy_loader.h"

#include <cstdio>

namespace bytetaper::runtime_policy {

RuntimePolicyPlane::RuntimePolicyPlane(RuntimePolicyPlaneConfig config)
    : config_(std::move(config)) {}

RuntimePolicyPlaneStartResult RuntimePolicyPlane::start() {
    // Persisted snapshots carry their own generation from metadata.
    // Bootstrap and empty snapshots must use the store's next_generation().
    std::uint64_t next_gen = config_.runtime_policy_store->next_generation();

    StartupPolicyLoader loader(config_);
    StartupPolicyLoadResult load_result = loader.load(next_gen);

    diagnostics_ = load_result.diagnostics;

    if (!load_result.ok) {
        status_.code = RuntimePolicyStatusCode::StartupFailed;
        status_.error_message = load_result.diagnostics.error_message;

        RuntimePolicyPlaneStartResult result{};
        result.ok = false;
        result.status = status_;
        result.diagnostics = diagnostics_;
        return result;
    }

    std::string install_err;
    if (!config_.runtime_policy_store->install_initial(load_result.snapshot, &install_err)) {
        std::fprintf(stderr, "failed to install initial policy snapshot: %s\n",
                     install_err.c_str());

        status_.code = RuntimePolicyStatusCode::StartupFailed;
        status_.error_message = install_err;
        diagnostics_.error_code = "install_initial_failed";
        diagnostics_.error_message = install_err;

        RuntimePolicyPlaneStartResult result{};
        result.ok = false;
        result.status = status_;
        result.diagnostics = diagnostics_;
        return result;
    }

    status_.code = load_result.status_code;

    RuntimePolicyPlaneStartResult result{};
    result.ok = true;
    result.status = status_;
    result.diagnostics = diagnostics_;
    return result;
}

const RuntimePolicyStatus& RuntimePolicyPlane::status() const {
    return status_;
}

const RuntimePolicyDiagnostics& RuntimePolicyPlane::diagnostics() const {
    return diagnostics_;
}

} // namespace bytetaper::runtime_policy

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_DIAGNOSTICS_H
#define BYTETAPER_RUNTIME_POLICY_DIAGNOSTICS_H

#include <cstdint>
#include <string>

namespace bytetaper::runtime_policy {

// Captures observable state from the startup policy load sequence.
// Intended for logging and future diagnostics endpoints.
struct RuntimePolicyDiagnostics {
    std::string loaded_source; // "persisted" | "bootstrap" | "empty"
    std::string policy_identity;
    std::uint64_t generation = 0;
    bool persistence_enabled = false;
    std::string policy_state_dir;
    std::string bootstrap_policy_file;
    std::string error_code;
    std::string error_message;
    // Future: expected_generation, canonical_hash, activation_barrier_stage
};

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_DIAGNOSTICS_H

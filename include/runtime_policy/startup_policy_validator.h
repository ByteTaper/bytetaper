// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_STARTUP_POLICY_VALIDATOR_H
#define BYTETAPER_RUNTIME_POLICY_STARTUP_POLICY_VALIDATOR_H

#include "control_plane/policy_lifecycle_emitter.h"
#include "control_plane/policy_state_store.h"
#include "runtime_policy/policy_mismatch_classifier.h"
#include "taperquery/policy_persistence.h"

namespace bytetaper::runtime_policy {

struct StartupValidationInput {
    const control_plane::PolicyResourceKey* resource_key = nullptr;
    const control_plane::ActivePolicyPointer* active_pointer = nullptr;
    const taperquery::PolicyPersistenceLoadResult* local_load_result = nullptr;
    control_plane::PolicyStateStore* store = nullptr;
    control_plane::PolicyLifecycleEmitter* lifecycle_emitter = nullptr;
    RuntimePolicyMetrics* runtime_policy_metrics = nullptr;
    bool validate_local_mirror = false;
};

struct StartupValidationResult {
    bool valid = false;
    RuntimePolicyHealth health = RuntimePolicyHealth::Unknown;
    std::string message;
    PolicyMismatchInput mismatch_detail{};
    std::string committed_canonical_yaml;
};

StartupValidationResult
validate_startup_against_active_pointer(const StartupValidationInput& input);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_STARTUP_POLICY_VALIDATOR_H

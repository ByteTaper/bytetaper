// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/startup_policy_validator.h"

#include "control_plane/policy_lifecycle_event.h"
#include "runtime_policy/runtime_policy_log_events.h"
#include "runtime_policy/runtime_policy_metrics.h"
#include "taperquery/policy_persistence.h"

namespace bytetaper::runtime_policy {

namespace {

bool metadata_is_valid(const taperquery::PersistedPolicyMetadata& meta) {
    if (meta.metadata_schema_version == 1) {
        return !meta.resource_key.empty() && meta.generation != 0 &&
               !meta.policy_identity.empty() && !meta.canonical_hash.empty();
    }
    return meta.generation != 0 &&
           (!meta.policy_identity.empty() || !meta.canonical_yaml_sha256.empty());
}

const char* mismatch_reason_for_health(RuntimePolicyHealth health) {
    switch (health) {
    case RuntimePolicyHealth::InactiveCorruptedLocal:
        return "corrupted";
    case RuntimePolicyHealth::InactiveStaleLocal:
        return "stale";
    case RuntimePolicyHealth::InactiveDivergedLocal:
        return "diverged";
    case RuntimePolicyHealth::InactiveMissingLocal:
        return "missing";
    case RuntimePolicyHealth::InactiveUnparseableLocal:
        return "unparseable";
    case RuntimePolicyHealth::BootstrapDriftDetected:
        return "bootstrap_drift";
    default:
        return "unknown";
    }
}

void emit_mismatch_if_needed(const StartupValidationInput& input,
                             const StartupValidationResult& result) {
    if (result.health == RuntimePolicyHealth::Active) {
        return;
    }
    control_plane::PolicyLifecycleEvent event{};
    event.event_type = control_plane::PolicyLifecycleEventType::PolicyMismatchDetected;
    event.resource_key = input.resource_key != nullptr ? input.resource_key->to_string() : "";
    event.before_generation = result.mismatch_detail.committed_generation;
    event.after_generation = result.mismatch_detail.local_generation;
    event.status = "failure";
    event.message = mismatch_reason_for_health(result.health);
    event.error_code = result.message;
    if (input.lifecycle_emitter != nullptr) {
        (void) input.lifecycle_emitter->emit(event);
    }
    record_mismatch(input.runtime_policy_metrics, event.message.c_str());
    log_runtime_policy_lifecycle_event(event);
}

std::string metadata_canonical_hash(const taperquery::PersistedPolicyMetadata& meta) {
    if (!meta.canonical_hash.empty()) {
        return meta.canonical_hash;
    }
    if (!meta.canonical_yaml_sha256.empty()) {
        return "sha256:" + meta.canonical_yaml_sha256;
    }
    return "";
}

} // namespace

StartupValidationResult
validate_startup_against_active_pointer(const StartupValidationInput& input) {
    StartupValidationResult result{};

    if (input.resource_key == nullptr || input.active_pointer == nullptr ||
        input.store == nullptr) {
        result.health = RuntimePolicyHealth::StartupFailed;
        result.message = "startup validation input is incomplete";
        return result;
    }
    if (input.validate_local_mirror && input.local_load_result == nullptr) {
        result.health = RuntimePolicyHealth::StartupFailed;
        result.message = "startup validation requires local mirror load result";
        return result;
    }

    const control_plane::ActivePolicyPointer& pointer = *input.active_pointer;

    auto version_res = input.store->load_policy_version(*input.resource_key, pointer.generation);
    if (!version_res.ok) {
        result.health = RuntimePolicyHealth::InactiveActivePointerMissingTarget;
        result.message = "committed policy version missing in store: " + version_res.error;
        return result;
    }

    result.committed_canonical_yaml = version_res.canonical_yaml;

    PolicyMismatchInput mismatch{};
    mismatch.committed_generation = pointer.generation;
    mismatch.committed_policy_id = pointer.policy_id;
    mismatch.committed_canonical_hash = pointer.canonical_hash;
    mismatch.version_record_exists = true;
    mismatch.version_hash_matches = strip_sha256_prefix(version_res.record.canonical_hash) ==
                                    strip_sha256_prefix(pointer.canonical_hash);
    mismatch.require_local_mirror = input.validate_local_mirror;

    const std::string yaml_hash_hex =
        taperquery::compute_canonical_yaml_sha256_hex(version_res.canonical_yaml);
    mismatch.committed_yaml_hash_matches =
        strip_sha256_prefix(pointer.canonical_hash) == yaml_hash_hex;

    if (!mismatch.committed_yaml_hash_matches) {
        result.mismatch_detail = mismatch;
        result.health = RuntimePolicyHealth::InactiveActivePointerMissingTarget;
        result.valid = false;
        result.message = "committed canonical yaml hash does not match active pointer";
        return result;
    }

    if (!input.validate_local_mirror) {
        result.mismatch_detail = mismatch;
        result.health = classify_mismatch(mismatch);
        if (result.health == RuntimePolicyHealth::Active) {
            result.valid = true;
            result.message = "active pointer matches committed store policy";
        } else {
            result.valid = false;
            result.message = "active pointer does not match committed store policy";
            emit_mismatch_if_needed(input, result);
        }
        return result;
    }

    const taperquery::PolicyPersistenceLoadResult& local = *input.local_load_result;

    mismatch.local_file_exists = !local.files_missing;
    mismatch.metadata_present = mismatch.local_file_exists;
    mismatch.local_file_parseable = local.ok;
    mismatch.metadata_valid = metadata_is_valid(local.metadata);

    if (local.ok) {
        mismatch.local_generation = local.metadata.generation;
        mismatch.local_policy_id = local.metadata.policy_identity;
        mismatch.local_canonical_hash = metadata_canonical_hash(local.metadata);
    } else if (mismatch.local_file_exists) {
        if (local.error.find("Failed to parse") != std::string::npos) {
            mismatch.local_file_parseable = false;
        } else if (local.error.find("integrity check failed") != std::string::npos ||
                   local.error.find("METADATA_CANONICAL_HASH_MISMATCH") != std::string::npos) {
            mismatch.local_file_parseable = true;
            mismatch.local_generation = local.metadata.generation;
            mismatch.local_policy_id = local.metadata.policy_identity;
            mismatch.local_canonical_hash = metadata_canonical_hash(local.metadata);
            mismatch.metadata_valid = metadata_is_valid(local.metadata);
        } else if (local.error.find("missing required fields") != std::string::npos ||
                   local.error.find("Metadata missing") != std::string::npos) {
            mismatch.metadata_valid = false;
        }
    }

    result.mismatch_detail = mismatch;
    result.health = classify_mismatch(mismatch);
    if (result.health == RuntimePolicyHealth::Active) {
        result.valid = true;
        result.message = "active pointer matches local mirror";
        return result;
    }

    result.valid = false;
    result.message = "active pointer does not match local mirror";
    emit_mismatch_if_needed(input, result);
    return result;
}

} // namespace bytetaper::runtime_policy

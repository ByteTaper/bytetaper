// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_generation_commit.h"

#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_state_key.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_roundtrip.h"
#include "taperquery/policy_persistence.h"

#include <chrono>

namespace bytetaper::control_plane {

namespace {

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

std::uint64_t resolve_adopt_candidate_generation(PolicyStateStore* store,
                                                 const PolicyResourceKey& key,
                                                 std::uint64_t active_generation,
                                                 std::uint64_t local_generation_hint) {
    if (store == nullptr) {
        return active_generation + 1;
    }

    std::uint64_t candidate = active_generation + 1;
    if (local_generation_hint > active_generation) {
        const LoadPolicyVersionResult existing =
            store->load_policy_version(key, local_generation_hint);
        if (!existing.ok) {
            candidate = local_generation_hint;
        }
    }
    return candidate;
}

PolicyGenerationCommitResult commit_policy_generation(PolicyStateStore* store,
                                                      const PolicyGenerationCommitInput& input,
                                                      const PolicyAuditRecord* success_audit) {
    PolicyGenerationCommitResult result{};

    if (store == nullptr) {
        result.error_code = "POLICY_APPLY_STORE_UNAVAILABLE";
        result.error_message = "policy state store is not configured";
        return result;
    }

    const LoadActivePointerResult active_res = store->load_active_pointer(input.resource_key);
    if (!active_res.ok) {
        result.error_code = "POLICY_APPLY_LOAD_ACTIVE_FAILED";
        result.error_message =
            active_res.error.empty() ? "active policy pointer not found" : active_res.error;
        return result;
    }

    const ActivePolicyPointer& active = active_res.pointer;
    if (active.generation != input.expected_active.generation ||
        active.policy_id != input.expected_active.policy_id) {
        result.error_code = kErrManualOperationPromoteConflict;
        result.error_message = "expected active policy does not match committed active pointer";
        return result;
    }

    const taperquery::PolicyIrLoadResult load_res = taperquery::load_policy_ir_from_yaml_string(
        input.canonical_yaml.data(), input.canonical_yaml.size());
    if (!load_res.ok) {
        result.error_code = kErrAdoptLocalPolicyInvalid;
        result.error_message =
            load_res.error.empty() ? "failed to parse policy yaml" : load_res.error;
        return result;
    }

    std::string stored_yaml;
    taperquery::TqPolicyDocument committed_ir = load_res.policy;
    std::string canonical_hash = input.canonical_hash_override;
    if (input.preserve_canonical_yaml) {
        stored_yaml = input.canonical_yaml;
    } else {
        const auto roundtrip = taperquery::emit_and_reparse_canonical_policy_yaml(load_res.policy);
        if (!roundtrip.ok) {
            result.error_code = kErrAdoptLocalPolicyInvalid;
            result.error_message =
                roundtrip.error.empty() ? "failed to canonicalize policy yaml" : roundtrip.error;
            return result;
        }
        stored_yaml = roundtrip.canonical_yaml;
        committed_ir = roundtrip.parsed_policy_ir;
    }

    const std::string candidate_policy_id =
        taperquery::compute_policy_document_identity(committed_ir);
    if (canonical_hash.empty()) {
        canonical_hash = "sha256:" + taperquery::compute_canonical_yaml_sha256_hex(stored_yaml);
    }

    const std::uint64_t candidate_generation = input.candidate_generation_override != 0
                                                   ? input.candidate_generation_override
                                                   : active.generation + 1;

    PolicyVersionRecord version_record;
    version_record.generation = candidate_generation;
    version_record.policy_id = candidate_policy_id;
    version_record.canonical_hash = canonical_hash;
    version_record.schema_version = committed_ir.schema_version_num;
    version_record.api_version = committed_ir.api_version;
    version_record.kind = committed_ir.kind;
    version_record.source_type = input.source_type.empty() ? "unknown" : input.source_type;
    version_record.apply_id = input.apply_id;
    version_record.previous_generation = active.generation;
    version_record.previous_policy_id = active.policy_id;
    version_record.created_at_unix_epoch_ms = now_ms();

    ActivePolicyPointer next_pointer;
    next_pointer.generation = candidate_generation;
    next_pointer.policy_id = candidate_policy_id;
    next_pointer.canonical_hash = canonical_hash;
    next_pointer.version_key = make_version_key(input.resource_key, candidate_generation);
    next_pointer.yaml_key = make_yaml_key(input.resource_key, candidate_generation);
    next_pointer.schema_version = version_record.schema_version;
    next_pointer.api_version = version_record.api_version;
    next_pointer.kind = version_record.kind;
    next_pointer.source_type = version_record.source_type;
    next_pointer.apply_id = input.apply_id;
    next_pointer.previous_generation = active.generation;
    next_pointer.previous_policy_id = active.policy_id;
    next_pointer.committed_at_unix_epoch_ms = now_ms();

    if (success_audit != nullptr) {
        PolicyAuditRecord audit = *success_audit;
        audit.after_generation = candidate_generation;
        audit.generation = candidate_generation;
        audit.policy_id = candidate_policy_id;

        CommitPolicyGenerationWithAuditParams params{};
        params.resource_key = input.resource_key;
        params.version = version_record;
        params.canonical_yaml = stored_yaml;
        params.expected_active = input.expected_active;
        params.next_pointer = next_pointer;
        params.audit = audit;

        const CommitPolicyGenerationWithAuditResult atomic_res =
            store->commit_policy_generation_with_audit(params);
        if (!atomic_res.ok) {
            if (atomic_res.code == PolicyStateErrorCode::AuditWriteFailed) {
                result.error_code = kErrManualAuditWriteFailed;
            } else if (atomic_res.code == PolicyStateErrorCode::VersionConflict ||
                       atomic_res.code == PolicyStateErrorCode::ActivePointerConflict) {
                result.error_code = kErrManualOperationPromoteConflict;
            } else if (atomic_res.code == PolicyStateErrorCode::DbOpenFailed ||
                       atomic_res.code == PolicyStateErrorCode::DbWriteFailed ||
                       atomic_res.code == PolicyStateErrorCode::DbReadFailed) {
                result.error_code = "POLICY_APPLY_STORE_VERSION_FAILED";
            } else {
                result.error_code = "POLICY_APPLY_PROMOTE_CONFLICT";
            }
            result.error_message = atomic_res.error;
            return result;
        }

        result.ok = true;
        result.new_generation = candidate_generation;
        result.new_policy_id = candidate_policy_id;
        result.new_canonical_hash = canonical_hash;
        result.promoted_pointer = next_pointer;
        return result;
    }

    const StorePolicyVersionResult store_res =
        store->store_policy_version(input.resource_key, version_record, stored_yaml);
    if (!store_res.ok) {
        if (store_res.code == PolicyStateErrorCode::VersionConflict) {
            result.error_code = kErrManualOperationPromoteConflict;
        } else {
            result.error_code = "POLICY_APPLY_STORE_VERSION_FAILED";
        }
        result.error_message = store_res.error;
        return result;
    }

    const PromoteActiveResult promote_res =
        store->compare_and_promote_active(input.resource_key, input.expected_active, next_pointer);
    if (!promote_res.ok) {
        if (promote_res.code == PolicyStateErrorCode::ActivePointerConflict) {
            result.error_code = kErrManualOperationPromoteConflict;
        } else {
            result.error_code = "POLICY_APPLY_PROMOTE_CONFLICT";
        }
        result.error_message = promote_res.error;
        return result;
    }

    result.ok = true;
    result.new_generation = candidate_generation;
    result.new_policy_id = candidate_policy_id;
    result.new_canonical_hash = canonical_hash;
    result.promoted_pointer = next_pointer;
    return result;
}

} // namespace bytetaper::control_plane

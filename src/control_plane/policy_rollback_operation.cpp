// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_rollback_operation.h"

#include "control_plane/control_plane_guardrails.h"
#include "control_plane/manual_resolution_audit.h"
#include "control_plane/policy_generation_commit.h"

namespace bytetaper::control_plane {

PolicyRollbackOperation::PolicyRollbackOperation(PolicyStateStore* store) : store_(store) {}

PolicyRollbackResult PolicyRollbackOperation::execute(const PolicyRollbackRequest& request) {
    PolicyRollbackResult result{};
    result.resource_key = request.resource_key.to_string();
    result.target_generation = request.target_generation;

    ManualResolutionValidationError validation_error{};
    if (!validate_rollback_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        result.message = validation_error.message;
        result.error_code = validation_error.error_code;
        return result;
    }

    const GuardrailResult confirm_guard =
        check_dangerous_operation_confirmation(DangerousOperationKind::Rollback, request.confirm);
    if (!confirm_guard.allowed) {
        result.status = confirm_guard.status;
        result.error_code = confirm_guard.error_code;
        result.error = confirm_guard.message;
        result.message = confirm_guard.message;
        return result;
    }

    if (store_ == nullptr) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error_code = kErrManualPolicyStateUnavailable;
        result.error = "policy state store is not configured";
        result.message = result.error;
        return result;
    }

    const LoadActivePointerResult active_res = store_->load_active_pointer(request.resource_key);
    if (!active_res.ok) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code = kErrManualInvalidRequest;
        result.error =
            active_res.error.empty() ? "no active policy pointer exists" : active_res.error;
        result.message = result.error;
        return result;
    }

    result.from_generation = active_res.pointer.generation;

    if (active_res.pointer.generation != request.expected_current_generation ||
        active_res.pointer.policy_id != request.expected_current_policy_id) {
        result.status = PolicyApplyStatus::RejectedCasMismatch;
        result.error_code = kErrRollbackExpectedCurrentMismatch;
        result.error = "expected current generation/policy does not match active pointer";
        result.message = result.error;
        return result;
    }

    const LoadPolicyVersionResult target_res =
        store_->load_policy_version(request.resource_key, request.target_generation);
    if (!target_res.ok) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code = kErrRollbackTargetNotFound;
        result.error = target_res.error.empty() ? "target generation not found" : target_res.error;
        result.message = result.error;
        return result;
    }

    PolicyGenerationCommitInput commit_input{};
    commit_input.resource_key = request.resource_key;
    commit_input.expected_active.generation = active_res.pointer.generation;
    commit_input.expected_active.policy_id = active_res.pointer.policy_id;
    commit_input.canonical_yaml = target_res.canonical_yaml;
    commit_input.preserve_canonical_yaml = true;
    commit_input.canonical_hash_override = target_res.record.canonical_hash;
    commit_input.source_type = "rollback";
    commit_input.apply_id = request.request_id;
    commit_input.operator_id = request.operator_id;
    commit_input.request_id = request.request_id;

    PolicyAuditRecord audit = make_manual_resolution_audit_record(
        PolicyLifecycleEventType::PolicyRollbackCompleted, request.resource_key, "rollback",
        "rollback", request.operator_id, request.request_id, "success", "", "", "",
        active_res.pointer.generation, request.target_generation, request.target_generation);

    const PolicyGenerationCommitResult commit_res =
        commit_policy_generation(store_, commit_input, &audit);
    if (!commit_res.ok) {
        result.status = PolicyApplyStatus::RejectedCasMismatch;
        if (commit_res.error_code == kErrManualOperationPromoteConflict) {
            result.error_code = kErrManualOperationPromoteConflict;
        } else {
            result.error_code = commit_res.error_code;
        }
        result.error = commit_res.error_message;
        result.message = commit_res.error_message;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::RollbackAccepted;
    result.new_generation = commit_res.new_generation;
    result.message =
        "Rollback accepted. A new generation will be created from target policy content.";
    return result;
}

} // namespace bytetaper::control_plane

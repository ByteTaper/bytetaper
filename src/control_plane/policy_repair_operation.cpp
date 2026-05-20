// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_repair_operation.h"

#include "control_plane/control_plane_guardrails.h"

namespace bytetaper::control_plane {

PolicyRepairOperation::PolicyRepairOperation(ControlPlaneServiceConfig config,
                                             PolicyStateStore* store,
                                             FleetStatusService* fleet_service)
    : config_(std::move(config)), store_(store), fleet_service_(fleet_service) {}

PolicyRepairLocalPlanResult
PolicyRepairOperation::plan(const PolicyRepairLocalPlanRequest& request) {
    PolicyRepairLocalPlanResult result{};
    result.resource_key = request.resource_key.to_string();
    result.runtime_id = request.runtime_id;

    ManualResolutionValidationError validation_error{};
    if (!validate_repair_local_plan_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        result.message = validation_error.message;
        result.error_code = validation_error.error_code;
        return result;
    }

    if (store_ == nullptr) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error_code = kErrManualPolicyStateUnavailable;
        result.error = "policy state store is not configured";
        result.message = result.error;
        return result;
    }

    if (fleet_service_ != nullptr &&
        !fleet_service_->has_runtime(request.resource_key, request.runtime_id)) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code = kErrRepairTargetRuntimeNotFound;
        result.error = "runtime is not registered in fleet status";
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

    result.committed_generation = active_res.pointer.generation;
    result.committed_policy_id = active_res.pointer.policy_id;
    result.committed_canonical_hash = active_res.pointer.canonical_hash;

    if (active_res.pointer.generation != request.expected_committed_generation ||
        active_res.pointer.policy_id != request.expected_committed_policy_id) {
        result.status = PolicyApplyStatus::RejectedCasMismatch;
        result.error_code = kErrRepairCommittedVersionMismatch;
        result.error = "expected committed generation/policy does not match active pointer";
        result.message = result.error;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::RepairLocalPlanReady;
    result.message = "Repair-local plan is ready.";
    return result;
}

PolicyRepairLocalResult PolicyRepairOperation::execute(const PolicyRepairLocalRequest& request) {
    PolicyRepairLocalResult result{};
    result.resource_key = request.resource_key.to_string();
    result.runtime_id = request.runtime_id;
    result.expected_committed_generation = request.expected_committed_generation;

    ManualResolutionValidationError validation_error{};
    if (!validate_repair_local_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        result.message = validation_error.message;
        result.error_code = validation_error.error_code;
        return result;
    }

    const GuardrailResult confirm_guard = check_dangerous_operation_confirmation(
        DangerousOperationKind::RepairLocal, request.confirm);
    if (!confirm_guard.allowed) {
        result.status = confirm_guard.status;
        result.error_code = confirm_guard.error_code;
        result.error = confirm_guard.message;
        result.message = confirm_guard.message;
        return result;
    }

    PolicyRepairLocalPlanRequest plan_request{};
    plan_request.resource_key = request.resource_key;
    plan_request.runtime_id = request.runtime_id;
    plan_request.expected_committed_generation = request.expected_committed_generation;
    plan_request.expected_committed_policy_id = request.expected_committed_policy_id;
    plan_request.operator_id = request.operator_id;
    plan_request.request_id = request.request_id;

    const PolicyRepairLocalPlanResult plan_result = plan(plan_request);
    if (!plan_result.ok) {
        result.status = plan_result.status;
        result.error_code = plan_result.error_code;
        result.error = plan_result.error;
        result.message = plan_result.message;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::RepairLocalAccepted;
    result.message = "Runtime local mirror repair accepted.";
    return result;
}

PolicyRepairLocalResult
PolicyRepairOperation::run_repair_hook(const PolicyRepairLocalRequest& request) {
    PolicyRepairLocalResult result{};
    result.resource_key = request.resource_key.to_string();
    result.runtime_id = request.runtime_id;
    result.expected_committed_generation = request.expected_committed_generation;

    if (!config_.repair_local_hook) {
        result.ok = true;
        result.status = PolicyApplyStatus::RepairLocalAccepted;
        result.message = "Runtime local mirror repair accepted.";
        return result;
    }

    const LoadActivePointerResult active_res = store_->load_active_pointer(request.resource_key);
    if (!active_res.ok) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error =
            active_res.error.empty() ? "no active policy pointer exists" : active_res.error;
        result.message = result.error;
        return result;
    }

    const RepairLocalHookResult hook_result =
        config_.repair_local_hook(request, active_res.pointer);
    if (!hook_result.ok) {
        result.status = PolicyApplyStatus::InternalError;
        result.error_code = hook_result.error_code.empty() ? kErrManualOperationActivationFailed
                                                           : hook_result.error_code;
        result.error = hook_result.error.empty() ? "repair-local hook failed" : hook_result.error;
        result.message = result.error;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::RepairLocalAccepted;
    result.message = "Runtime local mirror repair accepted.";
    return result;
}

} // namespace bytetaper::control_plane

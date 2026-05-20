// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/manual_resolution_service.h"

#include "control_plane/manual_resolution_audit.h"
#include "control_plane/policy_adopt_operation.h"
#include "control_plane/policy_lifecycle_event.h"
#include "control_plane/policy_repair_operation.h"
#include "control_plane/policy_rollback_operation.h"

namespace bytetaper::control_plane {

namespace {

constexpr const char* kAuditPersistError = "manual operation audit record could not be persisted";

bool emit_manual_lifecycle(ControlPlaneServiceConfig& config, const PolicyResourceKey& key,
                           PolicyLifecycleEventType type, const std::string& operation,
                           const std::string& source_type, const std::string& operator_id,
                           const std::string& request_id, bool success,
                           const std::string& failure_reason, std::uint64_t before_generation,
                           std::uint64_t after_generation, bool persist_audit) {
    if (config.lifecycle_emitter == nullptr) {
        return true;
    }
    PolicyLifecycleEvent event = make_manual_lifecycle_event(
        type, key, operation, source_type, operator_id, request_id, success ? "success" : "failure",
        failure_reason, "", "", before_generation, after_generation, after_generation);
    if (persist_audit) {
        return config.lifecycle_emitter->emit(event).ok;
    }
    config.lifecycle_emitter->emit_metrics_and_logs(event);
    return true;
}

void apply_audit_persistence_failure(bool& ok, PolicyApplyStatus& status, std::string& message,
                                     std::string& error, std::string& error_code) {
    if (!ok) {
        if (!error.empty()) {
            error += "; ";
        }
        error += kAuditPersistError;
        message = error;
        error_code = kErrManualAuditWriteFailed;
        return;
    }

    ok = false;
    status = PolicyApplyStatus::InternalError;
    error = kAuditPersistError;
    message = error;
    error_code = kErrManualAuditWriteFailed;
}

void normalize_manual_audit_commit_failure(PolicyRollbackResult& result) {
    if (result.error_code == kErrManualAuditWriteFailed) {
        apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                        result.error_code);
    }
}

void normalize_manual_audit_commit_failure(PolicyAdoptLocalResult& result) {
    if (result.error_code == kErrManualAuditWriteFailed) {
        apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                        result.error_code);
    }
}

} // namespace

ManualResolutionService::ManualResolutionService(ControlPlaneServiceConfig config,
                                                 FleetStatusService* fleet_service)
    : config_(std::move(config)), fleet_service_(fleet_service) {}

bool ManualResolutionService::record_manual_audit(const PolicyResourceKey& key,
                                                  const PolicyAuditRecord& record) {
    if (config_.policy_state_store == nullptr) {
        return false;
    }
    return config_.policy_state_store->append_audit_record(key, record).ok;
}

PolicyRepairLocalPlanResult
ManualResolutionService::plan_repair_local(const PolicyRepairLocalPlanRequest& request) {
    PolicyRepairOperation operation(config_, config_.policy_state_store, fleet_service_);
    return operation.plan(request);
}

PolicyRepairLocalResult
ManualResolutionService::repair_local(const PolicyRepairLocalRequest& request) {
    PolicyRepairLocalResult result{};
    if (!emit_manual_lifecycle(config_, request.resource_key,
                               PolicyLifecycleEventType::ManualRepairRequested, "repair-local",
                               "manual-repair", request.operator_id, request.request_id, true, "",
                               request.expected_committed_generation, 0, true)) {
        apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                        result.error_code);
        return result;
    }

    PolicyRepairOperation operation(config_, config_.policy_state_store, fleet_service_);
    result = operation.execute(request);

    const std::uint64_t generation = request.expected_committed_generation;
    if (!result.ok) {
        emit_manual_lifecycle(config_, request.resource_key,
                              PolicyLifecycleEventType::ManualRepairCompleted, "repair-local",
                              "manual-repair", request.operator_id, request.request_id, false,
                              result.error, generation, generation, true);
        return result;
    }

    PolicyAuditRecord audit = make_manual_resolution_audit_record(
        PolicyLifecycleEventType::ManualRepairCompleted, request.resource_key, "repair-local",
        "manual-repair", request.operator_id, request.request_id, "failure",
        "repair-local hook pending", "", "", generation, generation, generation);
    if (!record_manual_audit(request.resource_key, audit)) {
        apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                        result.error_code);
        return result;
    }

    PolicyRepairLocalResult hook_result = operation.run_repair_hook(request);
    audit = make_manual_resolution_audit_record(
        PolicyLifecycleEventType::ManualRepairCompleted, request.resource_key, "repair-local",
        "manual-repair", request.operator_id, request.request_id,
        hook_result.ok ? "success" : "failure", hook_result.ok ? "" : hook_result.error, "", "",
        generation, generation, generation);
    if (!record_manual_audit(request.resource_key, audit)) {
        apply_audit_persistence_failure(hook_result.ok, hook_result.status, hook_result.message,
                                        hook_result.error, hook_result.error_code);
        return hook_result;
    }

    emit_manual_lifecycle(config_, request.resource_key,
                          PolicyLifecycleEventType::ManualRepairCompleted, "repair-local",
                          "manual-repair", request.operator_id, request.request_id, hook_result.ok,
                          hook_result.error, generation, generation, false);

    return hook_result;
}

PolicyAdoptLocalPlanResult
ManualResolutionService::plan_adopt_local(const PolicyAdoptLocalPlanRequest& request) {
    PolicyAdoptOperation operation(config_, config_.policy_state_store, fleet_service_);
    return operation.plan(request);
}

PolicyAdoptLocalResult
ManualResolutionService::adopt_local(const PolicyAdoptLocalRequest& request) {
    PolicyAdoptLocalResult result{};
    if (!emit_manual_lifecycle(config_, request.resource_key,
                               PolicyLifecycleEventType::ManualAdoptRequested, "adopt-local",
                               "manual-adopt", request.operator_id, request.request_id, true, "",
                               request.expected_current_generation, 0, true)) {
        apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                        result.error_code);
        return result;
    }

    PolicyAdoptOperation operation(config_, config_.policy_state_store, fleet_service_);
    result = operation.execute(request);

    if (!result.ok) {
        emit_manual_lifecycle(config_, request.resource_key,
                              PolicyLifecycleEventType::ManualAdoptCompleted, "adopt-local",
                              "manual-adopt", request.operator_id, request.request_id, false,
                              result.error, request.expected_current_generation,
                              result.resolved_local_generation, true);
        normalize_manual_audit_commit_failure(result);
    } else {
        emit_manual_lifecycle(config_, request.resource_key,
                              PolicyLifecycleEventType::ManualAdoptCompleted, "adopt-local",
                              "manual-adopt", request.operator_id, request.request_id, true, "",
                              request.expected_current_generation, result.new_generation, false);
    }

    return result;
}

PolicyRollbackResult ManualResolutionService::rollback(const PolicyRollbackRequest& request) {
    PolicyRollbackResult result{};
    if (!emit_manual_lifecycle(
            config_, request.resource_key, PolicyLifecycleEventType::PolicyRollbackRequested,
            "rollback", "rollback", request.operator_id, request.request_id, true, "",
            request.expected_current_generation, request.target_generation, true)) {
        apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                        result.error_code);
        return result;
    }

    PolicyRollbackOperation operation(config_.policy_state_store);
    result = operation.execute(request);

    if (!result.ok) {
        emit_manual_lifecycle(
            config_, request.resource_key, PolicyLifecycleEventType::PolicyRollbackCompleted,
            "rollback", "rollback", request.operator_id, request.request_id, false, result.error,
            request.expected_current_generation, request.target_generation, true);
        normalize_manual_audit_commit_failure(result);
    } else {
        emit_manual_lifecycle(
            config_, request.resource_key, PolicyLifecycleEventType::PolicyRollbackCompleted,
            "rollback", "rollback", request.operator_id, request.request_id, true, "",
            request.expected_current_generation, request.target_generation, false);
    }

    return result;
}

} // namespace bytetaper::control_plane

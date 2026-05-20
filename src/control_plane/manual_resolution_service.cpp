// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/manual_resolution_service.h"

#include "control_plane/manual_resolution_audit.h"
#include "control_plane/policy_adopt_operation.h"
#include "control_plane/policy_repair_operation.h"
#include "control_plane/policy_rollback_operation.h"

namespace bytetaper::control_plane {

namespace {

constexpr const char* kAuditPersistError = "manual operation audit record could not be persisted";

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

PolicyAuditRecord make_failure_audit(const PolicyResourceKey& key, const std::string& operation,
                                     const std::string& source_type, const std::string& operator_id,
                                     const std::string& request_id,
                                     const std::string& failure_reason,
                                     std::uint64_t before_generation,
                                     std::uint64_t after_generation,
                                     std::uint64_t target_generation) {
    PolicyAuditRecord audit =
        make_manual_resolution_audit_record(key, operation, source_type, operator_id, request_id);
    audit.before_generation = before_generation;
    audit.after_generation = after_generation;
    audit.target_generation = target_generation;
    audit.result = "failure";
    audit.failure_reason = failure_reason;
    return audit;
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
    PolicyRepairOperation operation(config_, config_.policy_state_store, fleet_service_);
    PolicyRepairLocalResult result = operation.execute(request);

    const std::uint64_t generation = request.expected_committed_generation;
    if (!result.ok) {
        PolicyAuditRecord audit = make_failure_audit(
            request.resource_key, "repair-local", "manual-repair", request.operator_id,
            request.request_id, result.error, generation, generation, generation);
        if (!record_manual_audit(request.resource_key, audit)) {
            apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                            result.error_code);
        }
        return result;
    }

    PolicyAuditRecord audit =
        make_manual_resolution_audit_record(request.resource_key, "repair-local", "manual-repair",
                                            request.operator_id, request.request_id);
    audit.before_generation = generation;
    audit.after_generation = generation;
    audit.target_generation = generation;
    audit.result = "failure";
    audit.failure_reason = "repair-local hook pending";
    if (!record_manual_audit(request.resource_key, audit)) {
        apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                        result.error_code);
        return result;
    }

    PolicyRepairLocalResult hook_result = operation.run_repair_hook(request);
    audit =
        make_manual_resolution_audit_record(request.resource_key, "repair-local", "manual-repair",
                                            request.operator_id, request.request_id);
    audit.before_generation = generation;
    audit.after_generation = generation;
    audit.target_generation = generation;
    audit.result = hook_result.ok ? "success" : "failure";
    audit.failure_reason = hook_result.ok ? "" : hook_result.error;
    if (!record_manual_audit(request.resource_key, audit)) {
        apply_audit_persistence_failure(hook_result.ok, hook_result.status, hook_result.message,
                                        hook_result.error, hook_result.error_code);
        return hook_result;
    }

    return hook_result;
}

PolicyAdoptLocalPlanResult
ManualResolutionService::plan_adopt_local(const PolicyAdoptLocalPlanRequest& request) {
    PolicyAdoptOperation operation(config_, config_.policy_state_store, fleet_service_);
    return operation.plan(request);
}

PolicyAdoptLocalResult
ManualResolutionService::adopt_local(const PolicyAdoptLocalRequest& request) {
    PolicyAdoptOperation operation(config_, config_.policy_state_store, fleet_service_);
    PolicyAdoptLocalResult result = operation.execute(request);

    if (!result.ok) {
        PolicyAuditRecord audit = make_failure_audit(
            request.resource_key, "adopt-local", "manual-adopt", request.operator_id,
            request.request_id, result.error, request.expected_current_generation, 0,
            result.resolved_local_generation != 0 ? result.resolved_local_generation
                                                  : request.local_generation);
        if (!record_manual_audit(request.resource_key, audit)) {
            apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                            result.error_code);
        }
    }

    return result;
}

PolicyRollbackResult ManualResolutionService::rollback(const PolicyRollbackRequest& request) {
    PolicyRollbackOperation operation(config_.policy_state_store);
    PolicyRollbackResult result = operation.execute(request);

    if (!result.ok) {
        PolicyAuditRecord audit = make_failure_audit(
            request.resource_key, "rollback", "rollback", request.operator_id, request.request_id,
            result.error, request.expected_current_generation, 0, request.target_generation);
        if (!record_manual_audit(request.resource_key, audit)) {
            apply_audit_persistence_failure(result.ok, result.status, result.message, result.error,
                                            result.error_code);
        }
    }

    return result;
}

} // namespace bytetaper::control_plane

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_adopt_operation.h"

#include "control_plane/control_plane_guardrails.h"
#include "control_plane/manual_resolution_audit.h"
#include "control_plane/policy_generation_commit.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_ir_yaml_roundtrip.h"
#include "taperquery/policy_persistence.h"

namespace bytetaper::control_plane {

namespace {

std::string normalize_hash(const std::string& hash) {
    if (hash.rfind("sha256:", 0) == 0) {
        return hash;
    }
    if (hash.empty()) {
        return hash;
    }
    return "sha256:" + hash;
}

} // namespace

PolicyAdoptOperation::PolicyAdoptOperation(ControlPlaneServiceConfig config,
                                           PolicyStateStore* store,
                                           FleetStatusService* fleet_service)
    : config_(std::move(config)), store_(store), fleet_service_(fleet_service) {}

PolicyAdoptOperation::ResolvedLocalPolicy
PolicyAdoptOperation::fetch_local_policy(const PolicyAdoptLocalRequest& request) {
    ResolvedLocalPolicy result{};

    if (fleet_service_ == nullptr ||
        !fleet_service_->has_runtime(request.resource_key, request.runtime_id)) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code = kErrAdoptLocalPolicyUnavailable;
        result.error = "runtime local policy could not be fetched: runtime is not registered";
        return result;
    }

    const std::optional<RuntimeStatusReport> runtime_report =
        fleet_service_->find_runtime_report(request.resource_key, request.runtime_id);
    if (!runtime_report.has_value()) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code = kErrAdoptLocalPolicyUnavailable;
        result.error = "runtime local policy could not be fetched: runtime is not registered";
        return result;
    }

    if (!config_.adopt_local_fetch_hook) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error_code = kErrAdoptLocalPolicyUnavailable;
        result.error = "adopt-local fetch hook is not configured";
        return result;
    }

    const AdoptLocalFetchedPolicy fetched =
        config_.adopt_local_fetch_hook(request, *runtime_report);
    if (!fetched.ok) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code =
            fetched.error_code.empty() ? kErrAdoptLocalPolicyUnavailable : fetched.error_code;
        result.error =
            fetched.error.empty() ? "runtime local policy could not be fetched" : fetched.error;
        return result;
    }

    if (fetched.canonical_yaml.empty()) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code = kErrAdoptLocalPolicyUnavailable;
        result.error = "runtime local policy fetch returned empty canonical yaml";
        return result;
    }

    result.canonical_yaml = fetched.canonical_yaml;
    result.resolved_local_generation = fetched.local_generation;
    result.local_generation_hint =
        fetched.local_generation != 0 ? fetched.local_generation : request.local_generation;
    if (result.resolved_local_generation == 0) {
        result.resolved_local_generation = request.local_generation;
    }
    result.local_policy_id = fetched.local_policy_id;
    result.local_canonical_hash = fetched.local_canonical_hash;

    const taperquery::PolicyIrLoadResult load_res = taperquery::load_policy_ir_from_yaml_string(
        result.canonical_yaml.data(), result.canonical_yaml.size());
    if (!load_res.ok) {
        result.status = PolicyApplyStatus::RejectedParseError;
        result.error_code = kErrAdoptLocalPolicyInvalid;
        result.error =
            load_res.error.empty() ? "failed to parse fetched local policy yaml" : load_res.error;
        return result;
    }

    const auto roundtrip = taperquery::emit_and_reparse_canonical_policy_yaml(load_res.policy);
    if (!roundtrip.ok) {
        result.status = PolicyApplyStatus::RejectedValidation;
        result.error_code = kErrAdoptLocalPolicyInvalid;
        result.error = roundtrip.error.empty() ? "failed to canonicalize fetched local policy"
                                               : roundtrip.error;
        return result;
    }

    result.canonical_yaml = roundtrip.canonical_yaml;
    result.local_policy_id =
        taperquery::compute_policy_document_identity(roundtrip.parsed_policy_ir);
    result.local_canonical_hash =
        "sha256:" + taperquery::compute_canonical_yaml_sha256_hex(roundtrip.canonical_yaml);

    if (!request.local_policy_id.empty() && request.local_policy_id != result.local_policy_id) {
        result.status = PolicyApplyStatus::RejectedValidation;
        result.error_code = kErrAdoptLocalPolicyInvalid;
        result.error = "localPolicyId does not match fetched local policy identity";
        return result;
    }

    if (!request.local_canonical_hash.empty()) {
        const std::string normalized = normalize_hash(request.local_canonical_hash);
        if (normalized != result.local_canonical_hash) {
            result.status = PolicyApplyStatus::RejectedValidation;
            result.error_code = kErrAdoptLocalPolicyInvalid;
            result.error = "localCanonicalHash does not match fetched local policy hash";
            return result;
        }
    }

    result.ok = true;
    return result;
}

PolicyAdoptLocalPlanResult PolicyAdoptOperation::plan(const PolicyAdoptLocalPlanRequest& request) {
    PolicyAdoptLocalPlanResult result{};
    result.resource_key = request.resource_key.to_string();

    PolicyAdoptLocalRequest adopt_request{};
    adopt_request.resource_key = request.resource_key;
    adopt_request.runtime_id = request.runtime_id;
    adopt_request.expected_current_generation = request.expected_current_generation;
    adopt_request.local_generation = request.local_generation;
    adopt_request.local_policy_id = request.local_policy_id;
    adopt_request.local_canonical_hash = request.local_canonical_hash;
    adopt_request.operator_id = request.operator_id;
    adopt_request.request_id = request.request_id;
    adopt_request.confirm_divergent_adoption = true;

    ManualResolutionValidationError validation_error{};
    if (!validate_adopt_local_plan_request(request, &validation_error)) {
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

    const LoadActivePointerResult active_res = store_->load_active_pointer(request.resource_key);
    if (!active_res.ok) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error_code = kErrManualInvalidRequest;
        result.error =
            active_res.error.empty() ? "no active policy pointer exists" : active_res.error;
        result.message = result.error;
        return result;
    }

    const ResolvedLocalPolicy fetched = fetch_local_policy(adopt_request);
    if (!fetched.ok) {
        result.status = fetched.status;
        result.error_code = fetched.error_code;
        result.error = fetched.error;
        result.message = fetched.error;
        return result;
    }

    result.current_generation = active_res.pointer.generation;
    result.would_create_generation = resolve_adopt_candidate_generation(
        store_, request.resource_key, active_res.pointer.generation, fetched.local_generation_hint);

    const taperquery::PolicyIrLoadResult load_res = taperquery::load_policy_ir_from_yaml_string(
        fetched.canonical_yaml.data(), fetched.canonical_yaml.size());
    if (!load_res.ok) {
        result.status = PolicyApplyStatus::RejectedParseError;
        result.error_code = kErrAdoptLocalPolicyInvalid;
        result.error =
            load_res.error.empty() ? "failed to parse fetched local policy yaml" : load_res.error;
        result.message = result.error;
        return result;
    }

    result.candidate_policy_id = fetched.local_policy_id;
    result.candidate_canonical_hash = fetched.local_canonical_hash;
    result.route_count = load_res.policy.routes.size();

    result.ok = true;
    result.status = PolicyApplyStatus::AdoptLocalPlanReady;
    result.message = "Adopt-local plan is ready.";
    return result;
}

PolicyAdoptLocalResult PolicyAdoptOperation::execute(const PolicyAdoptLocalRequest& request) {
    PolicyAdoptLocalResult result{};
    result.resource_key = request.resource_key.to_string();

    ManualResolutionValidationError validation_error{};
    if (!validate_adopt_local_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        result.message = validation_error.message;
        result.error_code = validation_error.error_code;
        return result;
    }

    const GuardrailResult confirm_guard = check_dangerous_operation_confirmation(
        DangerousOperationKind::AdoptLocal, false, request.confirm_divergent_adoption);
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

    if (active_res.pointer.generation != request.expected_current_generation) {
        result.status = PolicyApplyStatus::RejectedCasMismatch;
        result.error_code = kErrRollbackExpectedCurrentMismatch;
        result.error = "expected current generation does not match active pointer";
        result.message = result.error;
        return result;
    }

    const ResolvedLocalPolicy fetched = fetch_local_policy(request);
    if (!fetched.ok) {
        result.status = fetched.status;
        result.error_code = fetched.error_code;
        result.error = fetched.error;
        result.message = fetched.error;
        return result;
    }

    result.resolved_local_generation = fetched.resolved_local_generation;

    PolicyGenerationCommitInput commit_input{};
    commit_input.resource_key = request.resource_key;
    commit_input.expected_active.generation = active_res.pointer.generation;
    commit_input.expected_active.policy_id = active_res.pointer.policy_id;
    commit_input.canonical_yaml = fetched.canonical_yaml;
    commit_input.source_type = "manual-adopt";
    commit_input.apply_id = request.request_id;
    commit_input.operator_id = request.operator_id;
    commit_input.request_id = request.request_id;
    commit_input.candidate_generation_override = resolve_adopt_candidate_generation(
        store_, request.resource_key, active_res.pointer.generation, fetched.local_generation_hint);

    PolicyAuditRecord audit = make_manual_resolution_audit_record(
        PolicyLifecycleEventType::ManualAdoptCompleted, request.resource_key, "adopt-local",
        "manual-adopt", request.operator_id, request.request_id, "success", "", "", "",
        request.expected_current_generation, fetched.resolved_local_generation,
        fetched.resolved_local_generation);

    const PolicyGenerationCommitResult commit_res =
        commit_policy_generation(store_, commit_input, &audit);
    if (!commit_res.ok) {
        result.status = PolicyApplyStatus::RejectedCasMismatch;
        result.error_code = commit_res.error_code;
        result.error = commit_res.error_message;
        result.message = commit_res.error_message;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::AdoptLocalAccepted;
    result.new_generation = commit_res.new_generation;
    result.new_policy_id = commit_res.new_policy_id;
    result.message = "Local policy adoption accepted as a new committed generation.";
    return result;
}

} // namespace bytetaper::control_plane

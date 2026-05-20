// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/manual_resolution_api.h"

namespace bytetaper::control_plane {

namespace {

bool resource_key_is_valid(const PolicyResourceKey& key) {
    return !key.namespace_name.empty() && !key.policy_name.empty();
}

bool request_id_present(const std::string& request_id) {
    return !request_id.empty();
}

void set_validation_error(ManualResolutionValidationError* error_out, const char* message) {
    if (error_out == nullptr) {
        return;
    }
    error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
    error_out->message = message;
    error_out->error_code = kErrManualInvalidRequest;
}

} // namespace

bool validate_repair_local_request(const PolicyRepairLocalRequest& request,
                                   ManualResolutionValidationError* error_out) {
    if (!resource_key_is_valid(request.resource_key)) {
        set_validation_error(error_out, "resourceKey is required");
        return false;
    }
    if (request.runtime_id.empty()) {
        set_validation_error(error_out, "runtimeId is required");
        return false;
    }
    if (request.expected_committed_generation == 0) {
        set_validation_error(error_out, "expectedCommittedGeneration is required");
        return false;
    }
    if (request.expected_committed_policy_id.empty()) {
        set_validation_error(error_out, "expectedCommittedPolicyId is required");
        return false;
    }
    if (!request_id_present(request.request_id)) {
        set_validation_error(error_out, "requestId is required");
        return false;
    }
    return true;
}

bool validate_repair_local_plan_request(const PolicyRepairLocalPlanRequest& request,
                                        ManualResolutionValidationError* error_out) {
    PolicyRepairLocalRequest equivalent{};
    equivalent.resource_key = request.resource_key;
    equivalent.runtime_id = request.runtime_id;
    equivalent.expected_committed_generation = request.expected_committed_generation;
    equivalent.expected_committed_policy_id = request.expected_committed_policy_id;
    equivalent.operator_id = request.operator_id;
    equivalent.request_id = request.request_id;
    equivalent.confirm = true;
    return validate_repair_local_request(equivalent, error_out);
}

bool validate_adopt_local_request(const PolicyAdoptLocalRequest& request,
                                  ManualResolutionValidationError* error_out) {
    if (!resource_key_is_valid(request.resource_key)) {
        set_validation_error(error_out, "resourceKey is required");
        return false;
    }
    if (request.runtime_id.empty()) {
        set_validation_error(error_out, "runtimeId is required");
        return false;
    }
    if (request.expected_current_generation == 0) {
        set_validation_error(error_out, "expectedCurrentGeneration is required");
        return false;
    }
    if (!request_id_present(request.request_id)) {
        set_validation_error(error_out, "requestId is required");
        return false;
    }
    return true;
}

bool validate_adopt_local_plan_request(const PolicyAdoptLocalPlanRequest& request,
                                       ManualResolutionValidationError* error_out) {
    PolicyAdoptLocalRequest equivalent{};
    equivalent.resource_key = request.resource_key;
    equivalent.runtime_id = request.runtime_id;
    equivalent.expected_current_generation = request.expected_current_generation;
    equivalent.local_generation = request.local_generation;
    equivalent.local_policy_id = request.local_policy_id;
    equivalent.local_canonical_hash = request.local_canonical_hash;
    equivalent.operator_id = request.operator_id;
    equivalent.request_id = request.request_id;
    equivalent.confirm_divergent_adoption = true;
    return validate_adopt_local_request(equivalent, error_out);
}

bool validate_rollback_request(const PolicyRollbackRequest& request,
                               ManualResolutionValidationError* error_out) {
    if (!resource_key_is_valid(request.resource_key)) {
        set_validation_error(error_out, "resourceKey is required");
        return false;
    }
    if (request.target_generation == 0) {
        set_validation_error(error_out, "targetGeneration is required");
        return false;
    }
    if (request.expected_current_generation == 0) {
        set_validation_error(error_out, "expectedCurrentGeneration is required");
        return false;
    }
    if (request.expected_current_policy_id.empty()) {
        set_validation_error(error_out, "expectedCurrentPolicyId is required");
        return false;
    }
    if (!request_id_present(request.request_id)) {
        set_validation_error(error_out, "requestId is required");
        return false;
    }
    return true;
}

} // namespace bytetaper::control_plane

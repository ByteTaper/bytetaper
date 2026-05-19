// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_apply_api.h"

namespace bytetaper::control_plane {

namespace {

bool resource_key_is_valid(const PolicyResourceKey& key) {
    return !key.namespace_name.empty() && !key.policy_name.empty();
}

bool source_type_is_valid(PolicyApplySourceType source_type) {
    return source_type == PolicyApplySourceType::TaperQuery ||
           source_type == PolicyApplySourceType::Yaml;
}

} // namespace

bool validate_dry_run_request(const PolicyDryRunRequest& request,
                              PolicyApplyValidationError* error_out) {
    if (!resource_key_is_valid(request.resource_key)) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "resourceKey is required";
        }
        return false;
    }
    if (!source_type_is_valid(request.source_type)) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "unsupported sourceType";
        }
        return false;
    }
    if (request.source.empty()) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "source is required";
        }
        return false;
    }
    if (request.strict_production && request.expected_base_policy_id.empty()) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "expectedBasePolicyId is required in strict production mode";
        }
        return false;
    }
    return true;
}

bool validate_apply_request(const PolicyApplyRequest& request,
                            PolicyApplyValidationError* error_out) {
    if (!resource_key_is_valid(request.resource_key)) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "resourceKey is required";
        }
        return false;
    }
    if (!source_type_is_valid(request.source_type)) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "unsupported sourceType";
        }
        return false;
    }
    if (request.source.empty()) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "source is required";
        }
        return false;
    }
    if (request.mode == PolicyApplyMode::Apply) {
        if (request.expected_base_generation == 0) {
            if (error_out != nullptr) {
                error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
                error_out->message = "expectedBaseGeneration is required for apply";
            }
            return false;
        }
        if (request.expected_base_policy_id.empty()) {
            if (error_out != nullptr) {
                error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
                error_out->message = "expectedBasePolicyId is required for apply";
            }
            return false;
        }
    }
    if (request.strict_production && request.expected_base_policy_id.empty()) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "expectedBasePolicyId is required in strict production mode";
        }
        return false;
    }
    return true;
}

bool validate_diff_request(const PolicyDiffRequest& request,
                           PolicyApplyValidationError* error_out) {
    PolicyDryRunRequest dry_run_equivalent{};
    dry_run_equivalent.resource_key = request.resource_key;
    dry_run_equivalent.source_type = request.source_type;
    dry_run_equivalent.source = request.source;
    dry_run_equivalent.expected_base_generation = request.expected_base_generation;
    dry_run_equivalent.expected_base_policy_id = request.expected_base_policy_id;
    dry_run_equivalent.operator_id = request.operator_id;
    dry_run_equivalent.request_id = request.request_id;
    dry_run_equivalent.strict_production = request.strict_production;
    dry_run_equivalent.include_unchanged_routes = request.include_unchanged_routes;
    dry_run_equivalent.include_field_level_changes = request.include_field_level_changes;
    return validate_dry_run_request(dry_run_equivalent, error_out);
}

bool validate_rollback_plan_request(const PolicyRollbackPlanRequest& request,
                                    PolicyApplyValidationError* error_out) {
    if (!resource_key_is_valid(request.resource_key)) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "resourceKey is required";
        }
        return false;
    }
    if (request.target_generation == 0) {
        if (error_out != nullptr) {
            error_out->status = PolicyApplyStatus::RejectedInvalidRequest;
            error_out->message = "targetGeneration is required";
        }
        return false;
    }
    return true;
}

} // namespace bytetaper::control_plane

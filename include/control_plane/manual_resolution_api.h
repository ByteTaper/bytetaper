// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_API_H
#define BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_API_H

#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/runtime_status_report.h"

#include <cstdint>
#include <functional>
#include <string>

namespace bytetaper::control_plane {

constexpr const char* kErrManualInvalidRequest = "MANUAL_INVALID_REQUEST";
constexpr const char* kErrManualConfirmationRequired = "MANUAL_CONFIRMATION_REQUIRED";
constexpr const char* kErrManualPolicyStateUnavailable = "MANUAL_POLICY_STATE_UNAVAILABLE";
constexpr const char* kErrRepairTargetRuntimeNotFound = "REPAIR_TARGET_RUNTIME_NOT_FOUND";
constexpr const char* kErrRepairCommittedVersionMismatch = "REPAIR_COMMITTED_VERSION_MISMATCH";
constexpr const char* kErrAdoptLocalPolicyUnavailable = "ADOPT_LOCAL_POLICY_UNAVAILABLE";
constexpr const char* kErrAdoptLocalPolicyInvalid = "ADOPT_LOCAL_POLICY_INVALID";
constexpr const char* kErrAdoptLocalConfirmationRequired = "ADOPT_LOCAL_CONFIRMATION_REQUIRED";
constexpr const char* kErrRollbackTargetNotFound = "ROLLBACK_TARGET_NOT_FOUND";
constexpr const char* kErrRollbackExpectedCurrentMismatch = "ROLLBACK_EXPECTED_CURRENT_MISMATCH";
constexpr const char* kErrRollbackConfirmationRequired = "ROLLBACK_CONFIRMATION_REQUIRED";
constexpr const char* kErrManualOperationPromoteConflict = "MANUAL_OPERATION_PROMOTE_CONFLICT";
constexpr const char* kErrManualOperationActivationFailed = "MANUAL_OPERATION_ACTIVATION_FAILED";
constexpr const char* kErrManualAuditWriteFailed = "MANUAL_AUDIT_WRITE_FAILED";

struct PolicyRepairLocalRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    std::string runtime_id;
    std::uint64_t expected_committed_generation = 0;
    std::string expected_committed_policy_id;
    bool confirm = false;
    std::string operator_id;
    std::string request_id;
};

struct PolicyRepairLocalPlanRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    std::string runtime_id;
    std::uint64_t expected_committed_generation = 0;
    std::string expected_committed_policy_id;
    std::string operator_id;
    std::string request_id;
};

struct PolicyRepairLocalResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::string runtime_id;
    std::uint64_t expected_committed_generation = 0;
    std::string message;
    std::string error;
    std::string error_code;
};

struct PolicyRepairLocalPlanResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::string runtime_id;
    std::uint64_t committed_generation = 0;
    std::string committed_policy_id;
    std::string committed_canonical_hash;
    std::string message;
    std::string error;
    std::string error_code;
};

struct AdoptLocalFetchedPolicy {
    bool ok = false;
    std::string canonical_yaml;
    std::uint64_t local_generation = 0;
    std::string local_policy_id;
    std::string local_canonical_hash;
    std::string error;
    std::string error_code;
};

struct PolicyAdoptLocalRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    std::string runtime_id;
    std::uint64_t expected_current_generation = 0;
    std::uint64_t local_generation = 0;
    std::string local_policy_id;
    std::string local_canonical_hash;
    bool confirm_divergent_adoption = false;
    std::string operator_id;
    std::string request_id;
};

struct PolicyAdoptLocalPlanRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    std::string runtime_id;
    std::uint64_t expected_current_generation = 0;
    std::uint64_t local_generation = 0;
    std::string local_policy_id;
    std::string local_canonical_hash;
    std::string operator_id;
    std::string request_id;
};

struct PolicyAdoptLocalResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::uint64_t new_generation = 0;
    std::string new_policy_id;
    std::uint64_t resolved_local_generation = 0;
    std::string message;
    std::string error;
    std::string error_code;
};

struct PolicyAdoptLocalPlanResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::uint64_t current_generation = 0;
    std::uint64_t would_create_generation = 0;
    std::string candidate_policy_id;
    std::string candidate_canonical_hash;
    std::size_t route_count = 0;
    std::string message;
    std::string error;
    std::string error_code;
};

struct PolicyRollbackRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    std::uint64_t target_generation = 0;
    std::uint64_t expected_current_generation = 0;
    std::string expected_current_policy_id;
    bool confirm = false;
    std::string operator_id;
    std::string request_id;
};

struct PolicyRollbackResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::uint64_t from_generation = 0;
    std::uint64_t target_generation = 0;
    std::uint64_t new_generation = 0;
    std::string message;
    std::string error;
    std::string error_code;
};

struct RepairLocalHookResult {
    bool ok = false;
    std::string error;
    std::string error_code;
};

using AdoptLocalFetchHook = std::function<AdoptLocalFetchedPolicy(
    const PolicyAdoptLocalRequest& request, const RuntimeStatusReport& runtime_report)>;

struct ManualResolutionValidationError {
    PolicyApplyStatus status = PolicyApplyStatus::RejectedInvalidRequest;
    std::string message;
    std::string error_code;
};

bool validate_repair_local_request(const PolicyRepairLocalRequest& request,
                                   ManualResolutionValidationError* error_out);

bool validate_repair_local_plan_request(const PolicyRepairLocalPlanRequest& request,
                                        ManualResolutionValidationError* error_out);

bool validate_adopt_local_request(const PolicyAdoptLocalRequest& request,
                                  ManualResolutionValidationError* error_out);

bool validate_adopt_local_plan_request(const PolicyAdoptLocalPlanRequest& request,
                                       ManualResolutionValidationError* error_out);

bool validate_rollback_request(const PolicyRollbackRequest& request,
                               ManualResolutionValidationError* error_out);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_API_H

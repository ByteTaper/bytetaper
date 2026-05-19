// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_APPLY_API_H
#define BYTETAPER_CONTROL_PLANE_POLICY_APPLY_API_H

#include "control_plane/policy_apply_contract.h"
#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::control_plane {

enum class PolicyApplySourceType {
    TaperQuery,
    Yaml,
};

enum class PolicyApplyMode {
    DryRun,
    Apply,
};

struct PolicyDryRunRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    PolicyApplySourceType source_type = PolicyApplySourceType::TaperQuery;
    std::string source;
    std::uint64_t expected_base_generation = 0;
    std::string expected_base_policy_id;
    std::string operator_id;
    std::string request_id;
    bool strict_production = true;
    bool include_unchanged_routes = false;
    bool include_field_level_changes = true;
};

struct PolicyDryRunResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::uint64_t base_generation = 0;
    std::string base_policy_id;
    std::uint64_t candidate_generation = 0;
    std::string candidate_policy_id;
    std::size_t route_count = 0;
    PolicyRouteDiffPlan plan{};
    std::vector<PolicyApplyDiagnostic> diagnostics;
    std::string error;
};

struct PolicyApplyRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    PolicyApplySourceType source_type = PolicyApplySourceType::TaperQuery;
    PolicyApplyMode mode = PolicyApplyMode::Apply;
    std::string source;
    std::uint64_t expected_base_generation = 0;
    std::string expected_base_policy_id;
    std::string operator_id;
    std::string request_id;
    bool strict_production = true;
    bool include_unchanged_routes = false;
    bool include_field_level_changes = true;
};

struct PolicyApplySubmitResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::string job_id;
    std::uint32_t logical_shard_id = 0;
    std::uint64_t expected_base_generation = 0;
    std::uint64_t actual_base_generation = 0;
    std::string expected_base_policy_id;
    std::string actual_base_policy_id;
    std::string message;
    std::string error;
};

struct PolicyDiffRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    PolicyApplySourceType source_type = PolicyApplySourceType::TaperQuery;
    std::string source;
    std::uint64_t expected_base_generation = 0;
    std::string expected_base_policy_id;
    std::string operator_id;
    std::string request_id;
    bool strict_production = true;
    bool include_unchanged_routes = false;
    bool include_field_level_changes = true;
};

struct PolicyDiffResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::string base_policy_id;
    std::string candidate_policy_id;
    PolicyRouteDiffPlan plan{};
    std::vector<PolicyApplyDiagnostic> diagnostics;
    std::string error;
};

struct PolicyRollbackPlanRequest {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    std::uint64_t target_generation = 0;
    std::string operator_id;
    std::string request_id;
};

struct PolicyRollbackPlanResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::uint64_t current_generation = 0;
    std::uint64_t target_generation = 0;
    std::string target_policy_id;
    std::uint64_t would_create_generation = 0;
    std::string message;
    std::string error;
};

struct ActivePolicyResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    ActivePolicyPointer active{};
    std::string error;
};

struct PolicyApplyValidationError {
    PolicyApplyStatus status = PolicyApplyStatus::RejectedInvalidRequest;
    std::string message;
};

bool validate_dry_run_request(const PolicyDryRunRequest& request,
                              PolicyApplyValidationError* error_out);

bool validate_apply_request(const PolicyApplyRequest& request,
                            PolicyApplyValidationError* error_out);

bool validate_diff_request(const PolicyDiffRequest& request, PolicyApplyValidationError* error_out);

bool validate_rollback_plan_request(const PolicyRollbackPlanRequest& request,
                                    PolicyApplyValidationError* error_out);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_APPLY_API_H

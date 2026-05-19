// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_service.h"

#include "control_plane/policy_apply_contract.h"
#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_normalize.h"
#include "taperquery/route_analysis.h"
#include "taperquery/tq_compiler.h"
#include "taperquery/tq_parser.h"

#include <chrono>
#include <sstream>

namespace bytetaper::control_plane {

namespace {

struct CandidateParseResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    taperquery::TqPolicyDocument policy{};
    std::vector<PolicyApplyDiagnostic> diagnostics;
    std::string error;
};

std::string make_job_id(const std::string& request_id) {
    if (!request_id.empty()) {
        return "policy-job-" + request_id;
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "policy-job-" + std::to_string(ms);
}

CandidateParseResult parse_candidate(PolicyApplySourceType source_type, const std::string& source,
                                     const std::string& expected_base_policy_id) {
    CandidateParseResult result{};

    if (source_type == PolicyApplySourceType::Yaml) {
        const taperquery::PolicyIrLoadResult load_res =
            taperquery::load_policy_ir_from_yaml_string(source.data(), source.size());
        if (!load_res.ok) {
            result.status = PolicyApplyStatus::RejectedParseError;
            result.error = load_res.error.empty() ? "failed to parse policy yaml" : load_res.error;
            return result;
        }
        result.policy = taperquery::normalize_policy_ir(load_res.policy);
    } else if (source_type == PolicyApplySourceType::TaperQuery) {
        taperquery::TqParseOptions parse_opts{};
        parse_opts.collect_all_diagnostics = true;
        parse_opts.allow_top_level_routes = true;

        const auto parse_res =
            taperquery::parse_taperquery_source(source.data(), source.size(), parse_opts);
        if (!parse_res.ok) {
            result.status = PolicyApplyStatus::RejectedParseError;
            result.error = "parsing TaperQuery source failed";
            for (std::size_t i = 0; i < parse_res.diagnostics.count; ++i) {
                const auto& diag = parse_res.diagnostics.diagnostics[i];
                PolicyApplyDiagnostic mapped;
                mapped.severity = (diag.severity == taperquery::TqDiagnosticSeverity::Error)
                                      ? "error"
                                      : "warning";
                mapped.code = "TQ_PARSE_ERROR";
                mapped.message = diag.message;
                result.diagnostics.push_back(std::move(mapped));
            }
            return result;
        }

        taperquery::TqCompileOptions compile_opts{};
        compile_opts.allow_partial_document = false;
        compile_opts.preserve_source_order = true;
        compile_opts.strict_duplicate_blocks = true;

        taperquery::TqDiagnosticBag compile_diags{};
        compile_diags.collect_all = true;
        taperquery::TqPolicyDocument candidate{};

        const bool compile_ok = taperquery::compile_taperquery_ast_to_policy_ir(
            parse_res.document, compile_opts, &candidate, &compile_diags);
        if (!compile_ok) {
            result.status = PolicyApplyStatus::RejectedCompileError;
            result.error = "compiling TaperQuery source failed";
            for (std::size_t i = 0; i < compile_diags.count; ++i) {
                const auto& diag = compile_diags.diagnostics[i];
                PolicyApplyDiagnostic mapped;
                mapped.severity = (diag.severity == taperquery::TqDiagnosticSeverity::Error)
                                      ? "error"
                                      : "warning";
                mapped.code = "TQ_COMPILE_ERROR";
                mapped.message = diag.message;
                result.diagnostics.push_back(std::move(mapped));
            }
            return result;
        }
        result.policy = taperquery::normalize_policy_ir(candidate);
    } else {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error = "unsupported sourceType";
        return result;
    }

    if (!expected_base_policy_id.empty()) {
        if (!result.policy.expected_base_sha.empty() &&
            result.policy.expected_base_sha != expected_base_policy_id) {
            result.status = PolicyApplyStatus::RejectedCasMismatch;
            result.error = "expected base policy id in source disagrees with request expected base";
            return result;
        }
        if (result.policy.expected_base_sha.empty()) {
            result.policy.expected_base_sha = expected_base_policy_id;
        }
    }

    result.ok = true;
    return result;
}

bool load_before_policy(PolicyStateStore* store, const PolicyResourceKey& key,
                        const ActivePolicyPointer* active_pointer,
                        taperquery::TqPolicyDocument* before_out, std::string* error_out) {
    if (store == nullptr || before_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "policy state store is not configured";
        }
        return false;
    }

    if (active_pointer == nullptr) {
        *before_out = taperquery::TqPolicyDocument{};
        return true;
    }

    const auto version_res = store->load_policy_version(key, active_pointer->generation);
    if (!version_res.ok) {
        if (error_out != nullptr) {
            *error_out = "failed to load committed base policy version: " + version_res.error;
        }
        return false;
    }

    const taperquery::PolicyIrLoadResult load_res = taperquery::load_policy_ir_from_yaml_string(
        version_res.canonical_yaml.data(), version_res.canonical_yaml.size());
    if (!load_res.ok) {
        if (error_out != nullptr) {
            *error_out = "failed to parse committed base policy yaml: " + load_res.error;
        }
        return false;
    }

    *before_out = taperquery::normalize_policy_ir(load_res.policy);
    before_out->generation = active_pointer->generation;
    return true;
}

bool check_generation_cas_mismatch(const ActivePolicyPointer& active,
                                   std::uint64_t expected_base_generation) {
    if (expected_base_generation == 0) {
        return false;
    }
    return active.generation != expected_base_generation;
}

bool check_policy_id_cas_mismatch(const ActivePolicyPointer& active,
                                  const std::string& expected_base_policy_id) {
    if (expected_base_policy_id.empty()) {
        return false;
    }
    return active.policy_id != expected_base_policy_id;
}

PolicyApplyStatus map_plan_blockers(const taperquery::TqApplyPlan& plan, bool* cas_mismatch_out) {
    bool cas_mismatch = false;
    for (const auto& issue : plan.issues) {
        if (issue.severity != taperquery::TqPlanSeverity::Blocker) {
            continue;
        }
        if (issue.code == "CAS_SHA_MISMATCH" || issue.code == "CAS_MISSING_BASE_SHA") {
            cas_mismatch = true;
        }
    }
    if (cas_mismatch_out != nullptr) {
        *cas_mismatch_out = cas_mismatch;
    }
    if (cas_mismatch) {
        return PolicyApplyStatus::RejectedCasMismatch;
    }
    if (!plan.ok) {
        return PolicyApplyStatus::RejectedValidation;
    }
    return PolicyApplyStatus::InternalError;
}

taperquery::TqApplyPlanOptions make_plan_options(bool include_unchanged_routes,
                                                 bool include_field_level_changes,
                                                 bool strict_production) {
    taperquery::TqApplyPlanOptions opts{};
    opts.include_unchanged_routes = include_unchanged_routes;
    opts.include_field_level_changes = include_field_level_changes;
    opts.strict_production = strict_production;
    return opts;
}

bool is_policy_inactive(const ControlPlaneServiceConfig& config,
                        const PolicyResourceKey& resource_key) {
    if (!config.is_policy_inactive) {
        return false;
    }
    return config.is_policy_inactive(resource_key);
}

} // namespace

ControlPlaneService::ControlPlaneService(ControlPlaneServiceConfig config)
    : config_(std::move(config)) {}

PolicyDryRunResult ControlPlaneService::dry_run(const PolicyDryRunRequest& request) {
    PolicyDryRunResult result{};
    result.resource_key = request.resource_key.to_string();

    PolicyApplyValidationError validation_error{};
    if (!validate_dry_run_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        return result;
    }

    if (config_.policy_state_store == nullptr) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = "policy state store is not configured";
        return result;
    }

    if (is_policy_inactive(config_, request.resource_key)) {
        result.status = PolicyApplyStatus::RejectedPolicyInactive;
        result.error = "policy is inactive and requires manual resolution";
        return result;
    }

    const CandidateParseResult parsed =
        parse_candidate(request.source_type, request.source, request.expected_base_policy_id);
    if (!parsed.ok) {
        result.status = parsed.status;
        result.error = parsed.error;
        result.diagnostics = parsed.diagnostics;
        return result;
    }

    const auto active_res = config_.policy_state_store->load_active_pointer(request.resource_key);
    if (!active_res.ok && !active_res.not_found) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = active_res.error;
        return result;
    }

    const ActivePolicyPointer* active_ptr = active_res.ok ? &active_res.pointer : nullptr;
    if (active_ptr != nullptr) {
        if (check_generation_cas_mismatch(*active_ptr, request.expected_base_generation) ||
            check_policy_id_cas_mismatch(*active_ptr, request.expected_base_policy_id)) {
            result.status = PolicyApplyStatus::RejectedCasMismatch;
            result.base_generation = active_ptr->generation;
            result.base_policy_id = active_ptr->policy_id;
            result.error = "expected base policy does not match active committed policy";
            return result;
        }
    }

    taperquery::TqPolicyDocument before{};
    std::string load_err;
    if (!load_before_policy(config_.policy_state_store, request.resource_key, active_ptr, &before,
                            &load_err)) {
        result.status = PolicyApplyStatus::InternalError;
        result.error = load_err;
        return result;
    }

    const auto plan_opts =
        make_plan_options(request.include_unchanged_routes, request.include_field_level_changes,
                          request.strict_production);
    const taperquery::TqApplyPlan plan =
        taperquery::build_taperquery_apply_plan(before, parsed.policy, plan_opts);

    result.diagnostics = map_plan_issues_to_diagnostics(plan);
    result.plan = compute_route_diff_plan(plan);
    result.base_policy_id = plan.before_policy_identity;
    result.candidate_policy_id = plan.after_policy_identity;
    result.route_count = parsed.policy.routes.size();
    if (active_ptr != nullptr) {
        result.base_generation = active_ptr->generation;
        result.candidate_generation = active_ptr->generation + 1;
    } else {
        result.candidate_generation = 1;
    }

    bool cas_mismatch = false;
    if (!plan.ok || !plan.issues.empty()) {
        bool has_blocker = false;
        for (const auto& issue : plan.issues) {
            if (issue.severity == taperquery::TqPlanSeverity::Blocker) {
                has_blocker = true;
                break;
            }
        }
        if (has_blocker) {
            result.status = map_plan_blockers(plan, &cas_mismatch);
            result.error = "dry-run plan contains blocker issues";
            result.ok = false;
            return result;
        }
    }

    const auto route_analysis = taperquery::analyze_taperquery_route_precedence(parsed.policy);
    if (!route_analysis.ok) {
        result.status = PolicyApplyStatus::RejectedValidation;
        result.error = "route precedence analysis failed";
        result.ok = false;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::DryRunReady;
    return result;
}

PolicyApplySubmitResult ControlPlaneService::apply(const PolicyApplyRequest& request) {
    PolicyApplySubmitResult result{};
    result.resource_key = request.resource_key.to_string();
    result.expected_base_generation = request.expected_base_generation;
    result.expected_base_policy_id = request.expected_base_policy_id;

    PolicyApplyValidationError validation_error{};
    if (!validate_apply_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        result.message = validation_error.message;
        return result;
    }

    if (config_.policy_state_store == nullptr) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = "policy state store is not configured";
        result.message = result.error;
        return result;
    }

    if (is_policy_inactive(config_, request.resource_key)) {
        result.status = PolicyApplyStatus::RejectedPolicyInactive;
        result.error = "policy is inactive and requires manual resolution";
        result.message = result.error;
        return result;
    }

    const auto active_res = config_.policy_state_store->load_active_pointer(request.resource_key);
    if (!active_res.ok) {
        if (active_res.not_found) {
            result.status = PolicyApplyStatus::RejectedCasMismatch;
            result.error = "no active policy pointer exists for CAS apply";
            result.message = result.error;
            return result;
        }
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = active_res.error;
        result.message = result.error;
        return result;
    }

    result.actual_base_generation = active_res.pointer.generation;
    result.actual_base_policy_id = active_res.pointer.policy_id;

    if (check_generation_cas_mismatch(active_res.pointer, request.expected_base_generation) ||
        check_policy_id_cas_mismatch(active_res.pointer, request.expected_base_policy_id)) {
        result.status = PolicyApplyStatus::RejectedCasMismatch;
        result.error = "expected base policy does not match active committed policy";
        result.message = "Active policy changed before this request could be applied.";
        return result;
    }

    const CandidateParseResult parsed =
        parse_candidate(request.source_type, request.source, request.expected_base_policy_id);
    if (!parsed.ok) {
        result.status = parsed.status;
        result.error = parsed.error;
        result.message = parsed.error;
        return result;
    }

    taperquery::TqPolicyDocument before{};
    std::string load_err;
    if (!load_before_policy(config_.policy_state_store, request.resource_key, &active_res.pointer,
                            &before, &load_err)) {
        result.status = PolicyApplyStatus::InternalError;
        result.error = load_err;
        result.message = load_err;
        return result;
    }

    const auto plan_opts =
        make_plan_options(request.include_unchanged_routes, request.include_field_level_changes,
                          request.strict_production);
    const taperquery::TqApplyPlan plan =
        taperquery::build_taperquery_apply_plan(before, parsed.policy, plan_opts);

    bool cas_mismatch = false;
    if (!plan.ok) {
        result.status = map_plan_blockers(plan, &cas_mismatch);
        result.error = "apply plan contains blocker issues";
        result.message = result.error;
        return result;
    }
    for (const auto& issue : plan.issues) {
        if (issue.severity == taperquery::TqPlanSeverity::Blocker) {
            result.status = map_plan_blockers(plan, &cas_mismatch);
            result.error = "apply plan contains blocker issues";
            result.message = result.error;
            return result;
        }
    }

    if (plan.before_policy_identity == plan.after_policy_identity) {
        result.status = PolicyApplyStatus::RejectedNoChanges;
        result.error = "candidate policy matches active committed policy";
        result.message = result.error;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::Accepted;
    result.job_id = make_job_id(request.request_id);
    result.message = "Policy apply request accepted by Control Plane.";
    return result;
}

PolicyDiffResult ControlPlaneService::diff(const PolicyDiffRequest& request) {
    PolicyDiffResult result{};
    result.resource_key = request.resource_key.to_string();

    PolicyApplyValidationError validation_error{};
    if (!validate_diff_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        return result;
    }

    PolicyDryRunRequest dry_run_request{};
    dry_run_request.resource_key = request.resource_key;
    dry_run_request.source_type = request.source_type;
    dry_run_request.source = request.source;
    dry_run_request.expected_base_generation = request.expected_base_generation;
    dry_run_request.expected_base_policy_id = request.expected_base_policy_id;
    dry_run_request.operator_id = request.operator_id;
    dry_run_request.request_id = request.request_id;
    dry_run_request.strict_production = request.strict_production;
    dry_run_request.include_unchanged_routes = request.include_unchanged_routes;
    dry_run_request.include_field_level_changes = request.include_field_level_changes;

    const PolicyDryRunResult dry_run_res = dry_run(dry_run_request);
    result.base_policy_id = dry_run_res.base_policy_id;
    result.candidate_policy_id = dry_run_res.candidate_policy_id;
    result.plan = dry_run_res.plan;
    result.diagnostics = dry_run_res.diagnostics;

    if (!dry_run_res.ok) {
        result.status = dry_run_res.status;
        result.error = dry_run_res.error;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::DiffReady;
    return result;
}

PolicyRollbackPlanResult
ControlPlaneService::plan_rollback(const PolicyRollbackPlanRequest& request) {
    PolicyRollbackPlanResult result{};
    result.resource_key = request.resource_key.to_string();
    result.target_generation = request.target_generation;

    PolicyApplyValidationError validation_error{};
    if (!validate_rollback_plan_request(request, &validation_error)) {
        result.status = validation_error.status;
        result.error = validation_error.message;
        result.message = validation_error.message;
        return result;
    }

    if (config_.policy_state_store == nullptr) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = "policy state store is not configured";
        result.message = result.error;
        return result;
    }

    const auto active_res = config_.policy_state_store->load_active_pointer(request.resource_key);
    if (!active_res.ok) {
        if (active_res.not_found) {
            result.status = PolicyApplyStatus::RejectedInvalidRequest;
            result.error = "no active policy pointer exists";
            result.message = result.error;
            return result;
        }
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = active_res.error;
        result.message = result.error;
        return result;
    }

    result.current_generation = active_res.pointer.generation;
    result.would_create_generation = active_res.pointer.generation + 1;

    const auto target_res = config_.policy_state_store->load_policy_version(
        request.resource_key, request.target_generation);
    if (!target_res.ok) {
        result.status = PolicyApplyStatus::RejectedInvalidRequest;
        result.error =
            "target generation is not present in policy state store: " + target_res.error;
        result.message = result.error;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::RollbackPlanReady;
    result.target_policy_id = target_res.record.policy_id;
    result.message = "Rollback plan is ready.";
    return result;
}

ActivePolicyResult ControlPlaneService::get_active_policy(const PolicyResourceKey& resource_key) {
    ActivePolicyResult result{};
    result.resource_key = resource_key.to_string();

    if (config_.policy_state_store == nullptr) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = "policy state store is not configured";
        return result;
    }

    const auto active_res = config_.policy_state_store->load_active_pointer(resource_key);
    if (!active_res.ok) {
        if (active_res.not_found) {
            result.status = PolicyApplyStatus::RejectedInvalidRequest;
            result.error = "active policy pointer not found";
            return result;
        }
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = active_res.error;
        return result;
    }

    result.ok = true;
    result.status = PolicyApplyStatus::Applied;
    result.active = active_res.pointer;
    return result;
}

PolicyVersionListResult
ControlPlaneService::list_policy_versions(const PolicyResourceKey& resource_key) {
    PolicyVersionListResult result{};
    result.resource_key = resource_key.to_string();

    if (config_.policy_state_store == nullptr) {
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = "policy state store is not configured";
        return result;
    }

    const auto active_res = config_.policy_state_store->load_active_pointer(resource_key);
    if (!active_res.ok) {
        if (active_res.not_found) {
            result.ok = true;
            result.status = PolicyApplyStatus::Applied;
            return result;
        }
        result.status = PolicyApplyStatus::RejectedStorageUnavailable;
        result.error = active_res.error;
        return result;
    }

    const auto version_res = config_.policy_state_store->load_policy_version(
        resource_key, active_res.pointer.generation);
    if (!version_res.ok) {
        result.status = PolicyApplyStatus::InternalError;
        result.error = version_res.error;
        return result;
    }

    PolicyVersionSummary summary{};
    summary.generation = version_res.record.generation;
    summary.policy_id = version_res.record.policy_id;
    summary.canonical_hash = version_res.record.canonical_hash;
    summary.source_type = version_res.record.source_type;
    summary.created_at_unix_epoch_ms = version_res.record.created_at_unix_epoch_ms;
    result.versions.push_back(std::move(summary));

    result.ok = true;
    result.status = PolicyApplyStatus::Applied;
    return result;
}

} // namespace bytetaper::control_plane

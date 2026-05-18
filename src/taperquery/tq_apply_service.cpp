// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_apply_service.h"

#include "taperquery/policy_ir_normalize.h"
#include "taperquery/policy_ir_validator.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_ir_yaml_roundtrip.h"
#include "taperquery/policy_persistence.h"
#include "taperquery/tq_apply_audit.h"
#include "taperquery/tq_cache_namespace_versioning.h"
#include "taperquery/tq_compiler.h"
#include "taperquery/tq_parser.h"

#include <chrono>

namespace bytetaper::taperquery {

namespace {

void map_parser_diagnostics(const TqDiagnosticBag& bag, const std::string& code,
                            std::vector<TqApplyDiagnostic>* out) {
    for (std::size_t i = 0; i < bag.count; ++i) {
        const auto& d = bag.diagnostics[i];
        TqApplyDiagnostic ad;
        ad.severity = (d.severity == TqDiagnosticSeverity::Error) ? "error" : "warning";
        ad.code = code;
        ad.reason = d.message;
        out->push_back(ad);
    }
}

class DefaultTqSnapshotBuilder : public TqSnapshotBuilder {
public:
    runtime::RuntimePolicySnapshotBuildResult build_snapshot(const TqPolicyDocument& policy_ir,
                                                             std::uint64_t generation) override {
        return runtime::build_runtime_policy_snapshot_from_ir(policy_ir, generation);
    }
};

DefaultTqSnapshotBuilder g_default_builder;

} // namespace

TqApplyService::TqApplyService(runtime::RuntimePolicyStore* policy_store,
                               TqSnapshotBuilder* builder, TqApplyAuditStore* audit_store,
                               LocalPolicyPersistenceConfig persistence_config,
                               runtime::RouteCacheEpochStore* epoch_store,
                               RouteCacheCleanupQueue* cleanup_queue, cache::L1Cache* l1_cache)
    : policy_store_(policy_store), builder_(builder ? builder : &g_default_builder),
      audit_store_(audit_store), persistence_config_(std::move(persistence_config)),
      epoch_store_(epoch_store), l1_cache_(l1_cache), cleanup_queue_(cleanup_queue) {}

TqApplyResult TqApplyService::execute(const TqApplyRequest& request) {
    TqApplyResult result = execute_impl(request);

    if (audit_store_ != nullptr && should_record(result, audit_store_->options())) {
        audit_store_->append(build_apply_audit_record(request, result));
    }

    return result;
}

TqApplyResult TqApplyService::execute_impl(const TqApplyRequest& request) {
    TqApplyResult result;

    // Step 1 - Validate service state and request shape
    if (!policy_store_) {
        result.ok = false;
        result.status = TqApplyStatus::InternalError;
        result.message = "policy store is not configured";
        return result;
    }
    if (request.source.empty()) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedInvalidRequest;
        result.message = "TaperQuery source is empty";
        return result;
    }
    if (request.source_type != TqApplySourceType::TaperQuery) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedInvalidRequest;
        result.message = "unsupported source type";
        return result;
    }
    if (request.mode == TqApplyMode::Apply && request.expected_base_identity.empty()) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedInvalidRequest;
        result.message = "expected base identity is required for apply mode";
        return result;
    }
    if (request.strict_production && request.expected_base_identity.empty()) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedInvalidRequest;
        result.message = "expected base identity is required in strict production mode";
        return result;
    }

    // Step 2 - Enter FIFO apply boundary
    auto lock = policy_store_->acquire_apply_lock();

    // Step 3 - Load active snapshot
    auto current_snapshot = policy_store_->load();
    if (!current_snapshot) {
        result.ok = false;
        result.status = TqApplyStatus::InternalError;
        result.message = "active policy snapshot is not installed";
        return result;
    }

    result.current_policy_identity = current_snapshot->policy_identity;
    result.expected_base_identity = request.expected_base_identity;
    result.before_generation = current_snapshot->generation;
    result.after_generation = current_snapshot->generation;

    // Step 4 - CAS check
    bool perform_cas = true;
    if (request.expected_base_identity.empty()) {
        if (request.mode == TqApplyMode::Apply || request.strict_production) {
            perform_cas = true;
        } else {
            perform_cas = false;
        }
    }

    if (perform_cas && request.expected_base_identity != current_snapshot->policy_identity) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedCasMismatch;
        result.message = "expected base identity does not match current active policy identity";

        TqApplyDiagnostic diag;
        diag.severity = "error";
        diag.code = "CAS_MISMATCH";
        diag.reason = result.message;
        diag.hint = "fetch the latest active policy and rebase your changes";
        result.diagnostics.push_back(diag);
        return result;
    }

    // Step 5 - Parse TaperQuery source
    TqParseOptions parse_opts;
    parse_opts.collect_all_diagnostics = true;
    parse_opts.allow_top_level_routes = true;

    auto parse_res =
        parse_taperquery_source(request.source.data(), request.source.size(), parse_opts);
    if (!parse_res.ok) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedParseError;
        result.message = "parsing TaperQuery source failed";
        map_parser_diagnostics(parse_res.diagnostics, "TQ_PARSE_ERROR", &result.diagnostics);
        return result;
    }

    // Step 6 - Compile AST to Policy IR
    TqCompileOptions compile_opts;
    compile_opts.allow_partial_document = false;
    compile_opts.preserve_source_order = true;
    compile_opts.strict_duplicate_blocks = true;

    TqPolicyDocument candidate;
    TqDiagnosticBag compile_diags{};
    compile_diags.collect_all = true;

    bool compile_ok = compile_taperquery_ast_to_policy_ir(parse_res.document, compile_opts,
                                                          &candidate, &compile_diags);
    if (!compile_ok) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedCompileError;
        result.message = "compiling TaperQuery AST to Policy IR failed";
        map_parser_diagnostics(compile_diags, "TQ_COMPILE_ERROR", &result.diagnostics);
        return result;
    }

    // Do not allow disagreement between request.expected_base_identity and
    // candidate.expected_base_sha
    if (!candidate.expected_base_sha.empty() &&
        candidate.expected_base_sha != request.expected_base_identity) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedCasMismatch;
        result.message = "expected base sha in TaperQuery document disagrees with request expected "
                         "base identity";

        TqApplyDiagnostic diag;
        diag.severity = "error";
        diag.code = "CAS_MISMATCH";
        diag.reason = result.message;
        result.diagnostics.push_back(diag);
        return result;
    }
    if (candidate.expected_base_sha.empty()) {
        candidate.expected_base_sha = request.expected_base_identity;
    }

    // Step 7 - Normalize candidate Policy IR
    candidate = normalize_policy_ir(candidate);

    // Step 8 - Validate candidate Policy IR
    TqPolicyValidationOptions val_opts;
    val_opts.collect_all = true;
    val_opts.include_warnings = true;
    val_opts.strict_production = request.strict_production;

    auto val_res = validate_taperquery_policy_ir(candidate, val_opts);
    bool val_has_errors = false;
    for (const auto& issue : val_res.issues) {
        if (issue.subsystem == TqPolicyValidationSubsystem::CrossRoute) {
            continue;
        }
        TqApplyDiagnostic ad;
        ad.severity = (issue.severity == TqPolicyValidationSeverity::Error) ? "error" : "warning";
        if (issue.severity == TqPolicyValidationSeverity::Error) {
            val_has_errors = true;
        }
        ad.code = "TQ_VALIDATION_ERROR";
        ad.route_id = issue.route_id;
        ad.field_path = issue.field_path;
        ad.reason = issue.reason;
        ad.hint = issue.hint;
        result.diagnostics.push_back(ad);
    }

    if (val_has_errors) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedValidation;
        result.message = "semantic validation of TaperQuery candidate failed";
        return result;
    }

    // Step 9 - Analyze route precedence
    auto route_analysis = analyze_taperquery_route_precedence(candidate);
    bool analysis_has_errors = false;
    for (const auto& finding : route_analysis.findings) {
        TqApplyDiagnostic ad;
        if (finding.severity == TqRouteAnalysisSeverity::Error) {
            ad.severity = "error";
            analysis_has_errors = true;
        } else if (finding.severity == TqRouteAnalysisSeverity::Warning) {
            ad.severity = "warning";
        } else {
            ad.severity = "info";
        }
        ad.code = "TQ_ROUTE_ANALYSIS_ERROR";
        ad.route_id = finding.route_id;
        ad.field_path = finding.field_path;
        ad.reason = finding.reason;
        if (!finding.related_route_id.empty()) {
            if (!ad.reason.empty()) {
                ad.reason += " ";
            }
            ad.reason += "(related route: " + finding.related_route_id + ")";
        }
        ad.hint = finding.hint;
        result.diagnostics.push_back(ad);
    }

    result.route_analysis = route_analysis;

    if (!route_analysis.ok || analysis_has_errors) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedRouteAnalysis;
        result.message = "route precedence and conflict analysis failed";
        return result;
    }

    // Step 10 - Build apply plan
    TqApplyPlanOptions plan_opts;
    plan_opts.include_unchanged_routes = request.include_unchanged_routes;
    plan_opts.include_field_level_changes = request.include_field_level_changes;
    plan_opts.strict_production = request.strict_production;

    auto plan = build_taperquery_apply_plan(current_snapshot->policy_ir, candidate, plan_opts);
    bool plan_has_blockers = false;
    bool plan_has_cas_blockers = false;

    for (const auto& issue : plan.issues) {
        TqApplyDiagnostic ad;
        ad.severity = (issue.severity == TqPlanSeverity::Blocker)
                          ? "error"
                          : ((issue.severity == TqPlanSeverity::Warning) ? "warning" : "info");
        if (issue.severity == TqPlanSeverity::Blocker) {
            plan_has_blockers = true;
            if (issue.code == "CAS_MISMATCH") {
                plan_has_cas_blockers = true;
            }
        }
        ad.code = (issue.code == "CAS_MISMATCH") ? "CAS_MISMATCH" : "TQ_PLAN_BLOCKER";
        ad.route_id = issue.route_id;
        ad.reason = issue.reason;
        ad.hint = issue.hint;
        result.diagnostics.push_back(ad);
    }

    result.plan = plan;

    if (!plan.ok || plan_has_blockers) {
        result.ok = false;
        result.status = plan_has_cas_blockers ? TqApplyStatus::RejectedCasMismatch
                                              : TqApplyStatus::RejectedValidation;
        result.message = "building apply plan failed due to blocker issues";
        return result;
    }

    // Step 11 - Reject no-op apply
    result.candidate_policy_identity = plan.after_policy_identity;
    if (plan.before_policy_identity == plan.after_policy_identity) {
        if (request.mode == TqApplyMode::Apply) {
            result.ok = false;
            result.status = TqApplyStatus::RejectedNoChanges;
            result.message = "no changes detected in candidate policy compared to active policy";
            return result;
        } else {
            result.ok = true;
            result.status = TqApplyStatus::DryRunReady;
            result.message = "no changes detected";
            return result;
        }
    }

    // Canonical YAML round-trip — must happen before any snapshot build or file write
    auto roundtrip = emit_and_reparse_canonical_policy_yaml(candidate);
    if (!roundtrip.ok) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedCanonicalYamlRoundTripFailed;
        result.message = "canonical YAML round-trip failed: " + roundtrip.error;

        TqApplyDiagnostic diag;
        diag.severity = "error";
        diag.code = "CANONICAL_YAML_ROUNDTRIP_FAILED";
        diag.reason = roundtrip.error;
        result.diagnostics.push_back(diag);
        return result;
    }
    if (roundtrip.candidate_policy_identity != roundtrip.persisted_policy_identity) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedCanonicalYamlMismatch;
        result.message = "canonical YAML round-trip identity mismatch: candidate=" +
                         roundtrip.candidate_policy_identity +
                         ", persisted=" + roundtrip.persisted_policy_identity;

        TqApplyDiagnostic diag;
        diag.severity = "error";
        diag.code = "CANONICAL_YAML_MISMATCH";
        diag.reason = result.message;
        result.diagnostics.push_back(diag);
        return result;
    }

    // Production-grade verification: build candidate snapshot using a non-mutating dummy generation
    // to catch compile/runtime failures without consuming a store generation.
    auto dummy_gen = current_snapshot->generation + 1;
    auto build_res = builder_->build_snapshot(roundtrip.parsed_policy_ir, dummy_gen);
    if (!build_res.ok) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedSnapshotBuildFailed;
        result.message = "building runtime policy snapshot failed: " + build_res.error;

        TqApplyDiagnostic diag;
        diag.severity = "error";
        diag.code = "TQ_SNAPSHOT_BUILD_FAILED";
        diag.reason = build_res.error;
        result.diagnostics.push_back(diag);
        return result;
    }

    // Step 12 - Dry-run exit
    if (request.mode == TqApplyMode::DryRun) {
        result.cache_namespace_versioning =
            detect_cache_namespace_impacts(result.plan, epoch_store_);
        result.ok = true;
        result.status = TqApplyStatus::DryRunReady;
        result.candidate_policy_identity = build_res.snapshot->policy_identity;
        result.message = "dry-run verification succeeded";
        return result;
    }

    // Step 13 - Generation sequence is committed on successful swap
    auto next_gen = current_snapshot->generation + 1;

    // Rebuild snapshot with the true incremented generation to ensure strict serial consistency
    auto final_build_res = builder_->build_snapshot(roundtrip.parsed_policy_ir, next_gen);
    if (!final_build_res.ok) {
        result.ok = false;
        result.status = TqApplyStatus::RejectedSnapshotBuildFailed;
        result.message = "building final runtime policy snapshot failed: " + final_build_res.error;
        return result;
    }

    // Persist before swap — durability before visibility
    if (persistence_config_.enabled && !persistence_config_.state_dir.empty()) {
        PersistedPolicyMetadata meta{};
        meta.policy_identity = final_build_res.snapshot->policy_identity;
        meta.candidate_policy_identity = roundtrip.candidate_policy_identity;
        meta.persisted_policy_identity = roundtrip.persisted_policy_identity;
        meta.previous_policy_identity = current_snapshot->policy_identity;
        meta.expected_base_identity = request.expected_base_identity;
        meta.generation = final_build_res.snapshot->generation;
        meta.source_type = "taperquery";
        meta.written_at_unix_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();
        meta.operator_id = request.operator_id;
        meta.request_id = request.request_id;

        auto persist_res = persist_active_policy_canonical_yaml(persistence_config_,
                                                                roundtrip.parsed_policy_ir, meta);

        if (!persist_res.ok) {
            result.ok = false;
            result.status = TqApplyStatus::RejectedPersistenceFailed;
            result.message = "persistence failed: " + persist_res.error;
            TqApplyDiagnostic diag;
            diag.severity = "error";
            diag.code = "PERSISTENCE_FAILED";
            diag.reason = persist_res.error;
            result.diagnostics.push_back(diag);
            return result;
        }
    }

    // Step 13.5 - Dry-run detect cache namespace impacts before swap (no mutation or cleanups)
    if (epoch_store_ != nullptr) {
        auto ns_detect = detect_cache_namespace_impacts(result.plan, epoch_store_);
        result.cache_namespace_versioning = ns_detect;
        if (!ns_detect.ok) {
            result.ok = false;
            result.status = TqApplyStatus::InternalError;
            result.message = "cache namespace versioning failed: " + ns_detect.error;

            TqApplyDiagnostic diag;
            diag.severity = "error";
            diag.code = "INTERNAL_ERROR";
            diag.reason = ns_detect.error;
            result.diagnostics.push_back(diag);
            return result;
        }
    }

    // Step 13.7 - Version/bump epochs in the RouteCacheEpochStore BEFORE swap
    TqCacheNamespaceApplyResult ns_res;
    if (epoch_store_ != nullptr) {
        ns_res = version_epochs_for_apply(result.plan, epoch_store_);
        if (!ns_res.ok) {
            result.ok = false;
            result.status = TqApplyStatus::InternalError;
            result.message = "cache namespace epoch versioning failed: " + ns_res.error;

            TqApplyDiagnostic diag;
            diag.severity = "error";
            diag.code = "INTERNAL_ERROR";
            diag.reason = ns_res.error;
            result.diagnostics.push_back(diag);
            return result;
        }
    }

    // Step 14 - Swap active snapshot via atomic CAS swap_if_current
    std::string swap_err;
    if (!policy_store_->swap_if_current(current_snapshot->policy_identity, final_build_res.snapshot,
                                        &swap_err)) {
        // Rollback any mutated epochs in epoch_store_ to enforce "failed applies must not bump"
        if (epoch_store_ != nullptr) {
            for (const auto& r : ns_res.routes) {
                if (r.old_epoch == 0) {
                    runtime::route_cache_epoch_remove(epoch_store_, r.route_id.c_str());
                } else {
                    runtime::route_cache_epoch_set(epoch_store_, r.route_id.c_str(), r.old_epoch);
                }
            }
        }

        if (swap_err.find("CAS mismatch") != std::string::npos) {
            result.ok = false;
            result.status = TqApplyStatus::RejectedCasMismatch;
            result.message = swap_err;

            TqApplyDiagnostic diag;
            diag.severity = "error";
            diag.code = "CAS_MISMATCH";
            diag.reason = swap_err;
            diag.hint = "fetch the latest active policy and rebase your changes";
            result.diagnostics.push_back(diag);
            return result;
        }

        result.ok = false;
        result.status = TqApplyStatus::InternalError;
        result.message = "failed to swap active policy snapshot: " + swap_err;

        TqApplyDiagnostic diag;
        diag.severity = "error";
        diag.code = "INTERNAL_ERROR";
        diag.reason = swap_err;
        result.diagnostics.push_back(diag);
        return result;
    }

    // Step 14.5 - Swap succeeded! Safe to perform actual L1 and L2 cleanups
    if (epoch_store_ != nullptr) {
        auto cleanup_res = cleanup_cache_namespaces_for_apply(ns_res, l1_cache_, cleanup_queue_);
        result.cache_namespace_apply_result = cleanup_res;

        // Repopulate for backward compatibility / exact verification
        result.cache_namespace_versioning.ok = cleanup_res.ok;
        result.cache_namespace_versioning.error = cleanup_res.error;
        result.cache_namespace_versioning.changed_routes.clear();
        for (const auto& r : cleanup_res.routes) {
            TqRouteCacheNamespaceChange ch;
            ch.route_id = r.route_id;
            ch.before_route_identity = r.before_route_identity;
            ch.after_route_identity = r.after_route_identity;
            ch.epoch_bump_required = r.l1_cleanup_required || r.l2_cleanup_required;
            ch.would_cleanup_l1 = r.l1_cleanup_required;
            ch.would_cleanup_l2 = r.l2_cleanup_required;
            ch.before_epoch = r.old_epoch;
            ch.after_epoch = r.new_epoch;
            ch.reasons = r.reasons;
            result.cache_namespace_versioning.changed_routes.push_back(ch);
        }

        if (!cleanup_res.ok) {
            result.ok = false;
            result.status = TqApplyStatus::InternalError;
            result.message = "cache namespace cleanup post-commit failed: " + cleanup_res.error;

            TqApplyDiagnostic diag;
            diag.severity = "error";
            diag.code = "INTERNAL_ERROR";
            diag.reason = cleanup_res.error;
            result.diagnostics.push_back(diag);
            return result;
        }
    }

    // Async cleanup: log and populate enqueued cleanups for maximum visibility
    for (const auto& r : result.cache_namespace_apply_result.routes) {
        if (r.l2_cleanup_required && r.old_epoch != 0) {
            std::string cleanup_job =
                "route:" + r.route_id + ":epoch:" + std::to_string(r.old_epoch);
            result.enqueued_cleanups.push_back(cleanup_job);

            std::fprintf(stdout,
                         "[RouteCacheCleanup] Enqueued async RouteCacheCleanupJob for route '%s' "
                         "(clearing old epoch namespace: %llu)\n",
                         r.route_id.c_str(), static_cast<unsigned long long>(r.old_epoch));
        }
    }

    result.ok = true;
    result.status = TqApplyStatus::Applied;
    result.candidate_policy_identity = final_build_res.snapshot->policy_identity;
    result.applied_policy_identity = final_build_res.snapshot->policy_identity;
    result.after_generation = final_build_res.snapshot->generation;
    result.message = "policy successfully applied";
    return result;
}

} // namespace bytetaper::taperquery

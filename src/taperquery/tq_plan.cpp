// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_plan.h"

#include "policy/policy_semantic_validator.h"
#include "policy/route_policy.h"
#include "taperquery/policy_ir_hash.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace bytetaper::taperquery {

namespace {

policy::RoutePolicy to_runtime_route_policy(const TqRoutePolicy& ir) {
    policy::RoutePolicy res{};
    res.route_id = ir.route_id.c_str();
    res.match_prefix = ir.match_prefix.c_str();
    res.match_kind = static_cast<policy::RouteMatchKind>(ir.match_kind);
    res.mutation = static_cast<policy::MutationMode>(ir.mutation);
    res.allowed_method = static_cast<policy::HttpMethod>(ir.allowed_method);

    res.field_filter.mode = static_cast<policy::FieldFilterMode>(ir.field_filter.mode);
    res.field_filter.field_count = std::min(ir.field_filter.fields.size(), policy::kMaxFields);
    for (std::size_t i = 0; i < res.field_filter.field_count; ++i) {
        std::strncpy(res.field_filter.fields[i], ir.field_filter.fields[i].c_str(),
                     policy::kMaxFieldNameLen - 1);
    }

    res.max_response_bytes = ir.max_response_bytes;

    res.cache.enabled = ir.cache.enabled;
    res.cache.behavior = static_cast<policy::CacheBehavior>(ir.cache.behavior);
    res.cache.ttl_seconds = ir.cache.ttl_ms / 1000u;
    res.cache.l1.enabled = ir.cache.l1.enabled;
    res.cache.l1.capacity_entries = ir.cache.l1.capacity_entries;
    res.cache.l2.enabled = ir.cache.l2.enabled;
    std::strncpy(res.cache.l2.path, ir.cache.l2.path.c_str(), policy::kMaxCachePathLen - 1);
    res.cache.private_cache = ir.cache.private_cache.enabled;
    std::strncpy(res.cache.auth_scope_header, ir.cache.private_cache.auth_scope_header.c_str(),
                 sizeof(res.cache.auth_scope_header) - 1);

    res.cache.field_variant.enabled = ir.cache.field_variant.enabled;
    res.cache.field_variant.max_variants_per_route = ir.cache.field_variant.max_variants_per_route;
    res.cache.field_variant.min_field_count = ir.cache.field_variant.min_field_count;
    res.cache.field_variant.max_field_count = ir.cache.field_variant.max_field_count;
    res.cache.field_variant.admission_threshold = ir.cache.field_variant.admission_threshold;
    res.cache.field_variant.ttl_max_ms = ir.cache.field_variant.ttl_max_ms;

    res.cache.vary_headers.count =
        std::min(ir.cache.vary_headers.names.size(), policy::kMaxCacheVaryHeaders);
    for (std::size_t i = 0; i < res.cache.vary_headers.count; ++i) {
        std::strncpy(res.cache.vary_headers.names[i], ir.cache.vary_headers.names[i].c_str(),
                     policy::kMaxCacheVaryHeaderNameLen - 1);
    }

    res.failure_mode = static_cast<policy::FailureMode>(ir.failure_mode);

    res.pagination.enabled = ir.pagination.enabled;
    res.pagination.mode = static_cast<policy::PaginationMode>(ir.pagination.mode);
    std::strncpy(res.pagination.limit_param, ir.pagination.limit_param.c_str(),
                 sizeof(res.pagination.limit_param) - 1);
    std::strncpy(res.pagination.offset_param, ir.pagination.offset_param.c_str(),
                 sizeof(res.pagination.offset_param) - 1);
    res.pagination.default_limit = ir.pagination.default_limit;
    res.pagination.max_limit = ir.pagination.max_limit;
    res.pagination.upstream_supports_pagination = ir.pagination.upstream_supports_pagination;
    res.pagination.max_response_bytes_warning = ir.pagination.max_response_bytes_warning;

    res.compression.enabled = ir.compression.enabled;
    res.compression.min_size_bytes = ir.compression.min_size_bytes;
    res.compression.eligible_content_type_count =
        std::min(ir.compression.eligible_content_types.size(), policy::kMaxEligibleContentTypes);
    for (std::size_t i = 0; i < res.compression.eligible_content_type_count; ++i) {
        std::strncpy(res.compression.eligible_content_types[i],
                     ir.compression.eligible_content_types[i].c_str(),
                     policy::kMaxContentTypeLen - 1);
    }
    res.compression.preferred_algorithm_count =
        std::min(ir.compression.preferred_algorithms.size(), policy::kMaxCompressionAlgorithms);
    for (std::size_t i = 0; i < res.compression.preferred_algorithm_count; ++i) {
        res.compression.preferred_algorithms[i] =
            static_cast<policy::CompressionAlgorithm>(ir.compression.preferred_algorithms[i]);
    }
    res.compression.already_encoded_behavior =
        static_cast<policy::AlreadyEncodedBehavior>(ir.compression.already_encoded_behavior);

    res.coalescing.enabled = ir.coalescing.enabled;
    res.coalescing.mode = static_cast<policy::CoalescingMode>(ir.coalescing.mode);
    res.coalescing.backend_timeout_ms = ir.coalescing.backend_timeout_ms;
    res.coalescing.handoff_buffer_ms = ir.coalescing.handoff_buffer_ms;
    res.coalescing.result_ready_retention_ms = ir.coalescing.result_ready_retention_ms;
    res.coalescing.max_waiters_per_key = ir.coalescing.max_waiters_per_key;
    res.coalescing.require_cache_enabled = ir.coalescing.require_cache_enabled;
    res.coalescing.allow_authenticated = ir.coalescing.allow_authenticated;
    res.coalescing.max_follower_wait_budget_ms = ir.coalescing.max_follower_wait_budget_ms;
    res.coalescing.max_active_follower_waiters = ir.coalescing.max_active_follower_waiters;
    res.coalescing.max_active_follower_waiters_per_shard =
        ir.coalescing.max_active_follower_waiters_per_shard;

    return res;
}

bool detect_duplicates(const TqPolicyDocument& doc, std::string& err_msg) {
    std::unordered_map<std::string, int> counts;
    for (const auto& r : doc.routes) {
        if (r.route_id.empty()) {
            err_msg = "Route ID cannot be empty";
            return true;
        }
        counts[r.route_id]++;
        if (counts[r.route_id] > 1) {
            err_msg = "Duplicate route ID found: " + r.route_id;
            return true;
        }
    }
    return false;
}

unsigned long long safe_stoull(const std::string& s, unsigned long long default_val = 0) {
    if (s.empty())
        return default_val;
    char* endptr = nullptr;
    unsigned long long val = std::strtoull(s.c_str(), &endptr, 10);
    if (endptr == s.c_str()) {
        return default_val;
    }
    return val;
}

TqRiskLevel classify_field_diff_risk(const PolicyIrFieldDiff& diff) {
    if (diff.field_path == "cache.ttl_ms") {
        unsigned long long before_ttl = safe_stoull(diff.expected);
        unsigned long long after_ttl = safe_stoull(diff.actual);
        if (after_ttl > before_ttl) {
            return TqRiskLevel::Low;
        } else {
            return TqRiskLevel::Medium;
        }
    }

    if (diff.field_path == "compression.min_size_bytes") {
        unsigned long long before_sz = safe_stoull(diff.expected);
        unsigned long long after_sz = safe_stoull(diff.actual);
        if (after_sz > before_sz) {
            return TqRiskLevel::Low;
        } else {
            return TqRiskLevel::Medium;
        }
    }

    if (diff.field_path == "pagination.max_response_bytes_warning") {
        return TqRiskLevel::Low;
    }

    if (diff.field_path == "cache.l1.capacity_entries" || diff.field_path == "field_filter.mode" ||
        diff.field_path == "field_filter.fields" ||
        diff.field_path == "compression.preferred_algorithms" ||
        diff.field_path == "pagination.default_limit" ||
        diff.field_path == "pagination.max_limit") {
        return TqRiskLevel::Medium;
    }

    if (diff.field_path == "match_prefix" || diff.field_path == "match_kind" ||
        diff.field_path == "allowed_method" || diff.field_path == "mutation" ||
        diff.field_path == "cache.enabled" || diff.field_path == "cache.l2.path" ||
        diff.field_path == "cache.private_cache.enabled" ||
        diff.field_path == "cache.private_cache.auth_scope_header" ||
        diff.field_path == "coalescing.enabled" ||
        diff.field_path == "coalescing.allow_authenticated") {
        return TqRiskLevel::High;
    }

    return TqRiskLevel::Medium;
}

TqRiskLevel classify_route_change_risk(const TqRouteChange& change) {
    TqRiskLevel max_risk = TqRiskLevel::None;
    for (const auto& diff : change.field_diffs) {
        TqRiskLevel r = classify_field_diff_risk(diff);
        if (r > max_risk) {
            max_risk = r;
        }
    }
    return max_risk;
}

void update_risk_summary(TqRiskSummary* summary, const TqRouteChange& change) {
    if (change.risk > summary->highest_risk) {
        summary->highest_risk = change.risk;
    }
    switch (change.kind) {
    case TqRouteChangeKind::Added:
        summary->added_routes++;
        break;
    case TqRouteChangeKind::Removed:
        summary->removed_routes++;
        break;
    case TqRouteChangeKind::Updated:
        summary->updated_routes++;
        break;
    case TqRouteChangeKind::Reordered:
        summary->reordered_routes++;
        break;
    default:
        break;
    }

    for (const auto& diff : change.field_diffs) {
        if (diff.field_path.rfind("cache.", 0) == 0) {
            summary->cache_changes++;
        } else if (diff.field_path.rfind("coalescing.", 0) == 0) {
            summary->coalescing_changes++;
        } else if (diff.field_path.rfind("compression.", 0) == 0) {
            summary->compression_changes++;
        } else if (diff.field_path.rfind("pagination.", 0) == 0) {
            summary->pagination_changes++;
        } else if (diff.field_path == "mutation") {
            summary->mutation_changes++;
        }
    }
}

} // namespace

bool build_taperquery_change_plan(const TqPolicyDocument& current_policy,
                                  const TqPolicyDocument& candidate_policy,
                                  const TqPlanOptions& options, TqPolicyChangePlan* out) {
    if (out == nullptr) {
        return false;
    }

    *out = TqPolicyChangePlan{};

    // Compute identities
    out->current_identity = compute_policy_document_identity(current_policy);
    out->candidate_identity = compute_policy_document_identity(candidate_policy);
    out->expected_base_identity = candidate_policy.expected_base_sha;

    // Detect duplicate route IDs in current or candidate
    std::string dup_err;
    if (detect_duplicates(current_policy, dup_err) ||
        detect_duplicates(candidate_policy, dup_err)) {
        out->status = TqPlanStatus::Error;
        out->message = dup_err;
        out->risk_summary.highest_risk = TqRiskLevel::Critical;
        return true;
    }

    // Evaluate CAS
    bool cas_failed = false;
    if (options.require_cas) {
        if (out->expected_base_identity.empty()) {
            out->cas_status = TqCasStatus::MissingExpectedBase;
            out->status = TqPlanStatus::BlockedByCasMismatch;
            out->message = "CAS precondition failed: candidate expected base identity is empty";
            cas_failed = true;
        } else if (out->expected_base_identity != out->current_identity) {
            out->cas_status = TqCasStatus::Mismatch;
            out->status = TqPlanStatus::BlockedByCasMismatch;
            out->message = "CAS precondition failed: candidate expected base " +
                           out->expected_base_identity + " but current policy is " +
                           out->current_identity;
            cas_failed = true;
        } else {
            out->cas_status = TqCasStatus::Match;
        }
    } else {
        out->cas_status = TqCasStatus::NotRequired;
    }

    // Validate candidate
    policy::PolicyFileResult runtime_file{};
    runtime_file.ok = true;
    runtime_file.count = std::min(candidate_policy.routes.size(), policy::kMaxRoutes);
    for (std::size_t i = 0; i < runtime_file.count; ++i) {
        const auto& tq_route = candidate_policy.routes[i];
        std::strncpy(runtime_file.route_id_storage[i], tq_route.route_id.c_str(),
                     policy::kMaxRouteIdLen - 1);
        std::strncpy(runtime_file.match_prefix_storage[i], tq_route.match_prefix.c_str(),
                     policy::kMaxPrefixLen - 1);

        runtime_file.policies[i] = to_runtime_route_policy(tq_route);
        runtime_file.policies[i].route_id = runtime_file.route_id_storage[i];
        runtime_file.policies[i].match_prefix = runtime_file.match_prefix_storage[i];
    }

    policy::PolicyValidationResult val_res{};
    policy::PolicyValidationOptions val_opts{};
    val_opts.collect_all = true;
    val_opts.include_warnings = true;

    bool val_ok = policy::validate_policy_file_semantic(runtime_file, &val_res, val_opts);
    out->semantic_validation_ok = val_ok;

    for (std::size_t i = 0; i < val_res.issue_count; ++i) {
        const auto& issue = val_res.issues[i];
        if (issue.severity == policy::PolicyValidationSeverity::Error) {
            out->risk_summary.validation_errors++;
        } else {
            out->risk_summary.validation_warnings++;
        }
    }

    // Build route indexes
    struct RouteInfo {
        std::size_t index;
        std::string identity;
        const TqRoutePolicy* ptr;
    };
    std::unordered_map<std::string, RouteInfo> current_routes;
    std::unordered_map<std::string, RouteInfo> candidate_routes;

    for (std::size_t i = 0; i < current_policy.routes.size(); ++i) {
        const auto& r = current_policy.routes[i];
        current_routes[r.route_id] = RouteInfo{ i, compute_route_policy_identity(r), &r };
    }
    for (std::size_t i = 0; i < candidate_policy.routes.size(); ++i) {
        const auto& r = candidate_policy.routes[i];
        candidate_routes[r.route_id] = RouteInfo{ i, compute_route_policy_identity(r), &r };
    }

    // Classify added, updated, reordered, and unchanged routes
    for (std::size_t after_idx = 0; after_idx < candidate_policy.routes.size(); ++after_idx) {
        const auto& cand_r = candidate_policy.routes[after_idx];
        std::string r_id = cand_r.route_id;

        if (current_routes.find(r_id) == current_routes.end()) {
            TqRouteChange change{};
            change.kind = TqRouteChangeKind::Added;
            change.route_id = r_id;
            change.after_index = after_idx;
            change.after_identity = candidate_routes[r_id].identity;
            change.risk = TqRiskLevel::Medium;
            change.summary = "route added";
            out->route_changes.push_back(change);
        } else {
            const auto& curr_info = current_routes[r_id];
            bool ident_changed = (curr_info.identity != candidate_routes[r_id].identity);
            bool order_chg = options.route_order_sensitive && (curr_info.index != after_idx);

            TqRouteChange change{};
            change.route_id = r_id;
            change.before_index = curr_info.index;
            change.after_index = after_idx;
            change.before_identity = curr_info.identity;
            change.after_identity = candidate_routes[r_id].identity;
            change.order_changed = order_chg;

            if (ident_changed) {
                change.kind = TqRouteChangeKind::Updated;

                TqPolicyDocument exp_doc{};
                exp_doc.routes.push_back(*curr_info.ptr);
                TqPolicyDocument act_doc{};
                act_doc.routes.push_back(cand_r);

                PolicyIrDiff route_diff = compare_policy_ir(exp_doc, act_doc);
                change.field_diffs = route_diff.field_diffs;

                change.risk = classify_route_change_risk(change);

                // Check for critical missing auth scope conditions
                if (cand_r.cache.private_cache.enabled &&
                    cand_r.cache.private_cache.auth_scope_header.empty()) {
                    change.risk = TqRiskLevel::Critical;
                }
                if (cand_r.coalescing.allow_authenticated &&
                    cand_r.cache.private_cache.auth_scope_header.empty()) {
                    change.risk = TqRiskLevel::Critical;
                }

                if (order_chg && change.risk < TqRiskLevel::High) {
                    change.risk = TqRiskLevel::High;
                }
                change.summary = "route updated";
                out->route_changes.push_back(change);
            } else if (order_chg) {
                change.kind = TqRouteChangeKind::Reordered;
                change.risk = TqRiskLevel::High;
                change.summary = "route reordered";
                out->route_changes.push_back(change);
            } else {
                if (options.include_unchanged_routes) {
                    change.kind = TqRouteChangeKind::Unchanged;
                    change.risk = TqRiskLevel::None;
                    change.summary = "route unchanged";
                    out->route_changes.push_back(change);
                }
            }
        }
    }

    // Classify removed routes
    for (std::size_t before_idx = 0; before_idx < current_policy.routes.size(); ++before_idx) {
        const auto& curr_r = current_policy.routes[before_idx];
        std::string r_id = curr_r.route_id;

        if (candidate_routes.find(r_id) == candidate_routes.end()) {
            TqRouteChange change{};
            change.kind = TqRouteChangeKind::Removed;
            change.route_id = r_id;
            change.before_index = before_idx;
            change.before_identity = current_routes[r_id].identity;
            change.risk = TqRiskLevel::High;
            change.summary = "route removed";
            out->route_changes.push_back(change);
        }
    }

    // Aggregate document field diffs
    PolicyIrDiff full_diff = compare_policy_ir(current_policy, candidate_policy);
    for (const auto& fd : full_diff.field_diffs) {
        if (fd.route_id == "<document>") {
            out->document_field_diffs.push_back(fd);
        }
    }

    // Aggregate risk summary
    for (const auto& change : out->route_changes) {
        update_risk_summary(&out->risk_summary, change);
    }

    // Elevate highest risk to Critical if validation errors occur or CAS fails
    if (out->risk_summary.validation_errors > 0 || cas_failed) {
        out->risk_summary.highest_risk = TqRiskLevel::Critical;
    }

    // Finalize plan status
    if (cas_failed) {
        out->status = TqPlanStatus::BlockedByCasMismatch;
    } else if (out->risk_summary.validation_errors > 0 ||
               (options.fail_on_validation_warning && out->risk_summary.validation_warnings > 0)) {
        out->status = TqPlanStatus::BlockedByValidation;
        if (out->risk_summary.validation_errors > 0) {
            out->message = std::string("Validation failed: ") + val_res.issues[0].reason;
        } else {
            out->message = std::string("Validation warning (fail_on_validation_warning=true): ") +
                           val_res.issues[0].reason;
        }
    } else {
        out->status = TqPlanStatus::Ready;
        if (out->candidate_identity == out->current_identity) {
            out->message = "No changes";
        } else {
            out->message = "Plan ready to apply";
        }
    }

    return true;
}

TqApplyDecision evaluate_apply_decision(const TqPolicyChangePlan& plan) {
    if (plan.status == TqPlanStatus::Error) {
        return TqApplyDecision::DeniedPlanError;
    }
    if (plan.status == TqPlanStatus::BlockedByCasMismatch) {
        return TqApplyDecision::DeniedCasMismatch;
    }
    if (plan.status == TqPlanStatus::BlockedByValidation) {
        return TqApplyDecision::DeniedValidationFailed;
    }
    if (plan.candidate_identity == plan.current_identity) {
        return TqApplyDecision::DeniedNoChanges;
    }
    return TqApplyDecision::Allowed;
}

} // namespace bytetaper::taperquery

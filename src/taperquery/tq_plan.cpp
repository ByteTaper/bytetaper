// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_plan.h"

#include "policy/route_policy.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_validator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace bytetaper::taperquery {

namespace {

TqSemanticImpact map_field_path_to_semantic_impact(const std::string& path) {
    if (path == "match_prefix" || path == "match_kind" || path == "allowed_method") {
        return TqSemanticImpact::MatchBehavior;
    }
    if (path == "mutation") {
        return TqSemanticImpact::MutationBehavior;
    }
    if (path.rfind("cache.l1.", 0) == 0 || path.rfind("cache.l2.", 0) == 0) {
        return TqSemanticImpact::CacheStorageBehavior;
    }
    if (path.rfind("cache.field_variant.", 0) == 0 || path == "cache.vary_headers.names" ||
        path == "cache.vary_headers" || path.rfind("cache.private_cache.", 0) == 0) {
        return TqSemanticImpact::CacheKeyBehavior;
    }
    if (path.rfind("cache.", 0) == 0) {
        return TqSemanticImpact::CacheBehavior;
    }
    if (path.rfind("field_filter.", 0) == 0) {
        return TqSemanticImpact::FieldFilteringBehavior;
    }
    if (path.rfind("pagination.", 0) == 0) {
        return TqSemanticImpact::PaginationBehavior;
    }
    if (path.rfind("compression.", 0) == 0) {
        return TqSemanticImpact::CompressionBehavior;
    }
    if (path.rfind("coalescing.", 0) == 0) {
        return TqSemanticImpact::CoalescingBehavior;
    }
    if (path == "failure_mode") {
        return TqSemanticImpact::FailureBehavior;
    }
    if (path.rfind("version.", 0) == 0) {
        return TqSemanticImpact::RuntimeCompatibility;
    }
    return TqSemanticImpact::None;
}

std::string semantic_impact_to_string(TqSemanticImpact impact) {
    switch (impact) {
    case TqSemanticImpact::MatchBehavior:
        return "MatchBehavior";
    case TqSemanticImpact::MutationBehavior:
        return "MutationBehavior";
    case TqSemanticImpact::CacheBehavior:
        return "CacheBehavior";
    case TqSemanticImpact::CacheKeyBehavior:
        return "CacheKeyBehavior";
    case TqSemanticImpact::CacheStorageBehavior:
        return "CacheStorageBehavior";
    case TqSemanticImpact::FieldFilteringBehavior:
        return "FieldFilteringBehavior";
    case TqSemanticImpact::PaginationBehavior:
        return "PaginationBehavior";
    case TqSemanticImpact::CompressionBehavior:
        return "CompressionBehavior";
    case TqSemanticImpact::CoalescingBehavior:
        return "CoalescingBehavior";
    case TqSemanticImpact::FailureBehavior:
        return "FailureBehavior";
    case TqSemanticImpact::RuntimeCompatibility:
        return "RuntimeCompatibility";
    default:
        return "None";
    }
}

std::string route_change_kind_to_string(TqRouteChangeKind kind) {
    switch (kind) {
    case TqRouteChangeKind::Added:
        return "Added";
    case TqRouteChangeKind::Removed:
        return "Removed";
    case TqRouteChangeKind::Modified:
        return "Modified";
    default:
        return "Unchanged";
    }
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
    case TqLegacyRouteChangeKind::Added:
        summary->added_routes++;
        break;
    case TqLegacyRouteChangeKind::Removed:
        summary->removed_routes++;
        break;
    case TqLegacyRouteChangeKind::Updated:
        summary->updated_routes++;
        break;
    case TqLegacyRouteChangeKind::Reordered:
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

} // namespace

// ============================================================================
// GA-spec TqApplyPlan Model Implementation
// ============================================================================

TqApplyPlan build_taperquery_apply_plan(const TqPolicyDocument& before,
                                        const TqPolicyDocument& after,
                                        const TqApplyPlanOptions& options) {
    TqApplyPlan plan;

    // 1. Compute Identities
    plan.before_policy_identity = compute_policy_document_identity(before);
    plan.after_policy_identity = compute_policy_document_identity(after);
    plan.expected_base_identity = after.expected_base_sha;

    // 2. CAS precondition checks (strict CAS is checked against before content identity)
    if (options.strict_production) {
        if (plan.expected_base_identity.empty()) {
            TqApplyPlanIssue issue;
            issue.severity = TqPlanSeverity::Blocker;
            issue.code = "CAS_MISSING_BASE_SHA";
            issue.reason = "Expected base SHA is empty inside the candidate policy";
            issue.hint = "Provide the current active policy's SHA in the 'expected_base_sha' field "
                         "of the apply payload.";
            plan.issues.push_back(issue);
            plan.ok = false;
        } else if (plan.expected_base_identity != plan.before_policy_identity) {
            TqApplyPlanIssue issue;
            issue.severity = TqPlanSeverity::Blocker;
            issue.code = "CAS_SHA_MISMATCH";
            issue.reason = "Optimistic locking concurrency check failed. Expected base SHA '" +
                           plan.expected_base_identity + "' does not match active policy SHA '" +
                           plan.before_policy_identity + "'.";
            issue.hint =
                "Pull the latest active policy, apply your changes on top of it, and try again.";
            plan.issues.push_back(issue);
            plan.ok = false;
        }
    }

    // 3. Semantic Validation of the Candidate Policy
    TqPolicyValidationOptions val_opts;
    val_opts.collect_all = true;
    val_opts.include_warnings = true;
    val_opts.strict_production = options.strict_production;

    auto val_res = validate_taperquery_policy_ir(after, val_opts);
    if (!val_res.ok) {
        plan.ok = false;
    }

    for (const auto& native_issue : val_res.issues) {
        TqApplyPlanIssue issue;
        issue.severity = (native_issue.severity == TqPolicyValidationSeverity::Error)
                             ? TqPlanSeverity::Blocker
                             : TqPlanSeverity::Warning;
        issue.code = native_issue.code;
        issue.route_id = native_issue.route_id;
        issue.reason = native_issue.reason;
        issue.hint = native_issue.hint;
        plan.issues.push_back(issue);
    }

    // 4. Map Route Changes (Added, Removed, Modified, Unchanged)
    std::unordered_map<std::string, const TqRoutePolicy*> before_routes;
    for (const auto& r : before.routes) {
        before_routes[r.route_id] = &r;
    }

    std::unordered_map<std::string, const TqRoutePolicy*> after_routes;
    for (const auto& r : after.routes) {
        after_routes[r.route_id] = &r;
    }

    // Identify Added & Modified/Unchanged routes
    for (const auto& after_route : after.routes) {
        std::string r_id = after_route.route_id;
        if (before_routes.find(r_id) == before_routes.end()) {
            // Added route
            TqApplyPlanRouteChange route_change;
            route_change.route_id = r_id;
            route_change.kind = TqRouteChangeKind::Added;
            route_change.after_identity = compute_route_policy_identity(after_route);
            plan.route_changes.push_back(route_change);
        } else {
            // Match found -> Compute differences
            const auto* before_route = before_routes[r_id];
            std::string before_ident = compute_route_policy_identity(*before_route);
            std::string after_ident = compute_route_policy_identity(after_route);

            if (before_ident != after_ident) {
                // Modified route
                TqApplyPlanRouteChange route_change;
                route_change.route_id = r_id;
                route_change.kind = TqRouteChangeKind::Modified;
                route_change.before_identity = before_ident;
                route_change.after_identity = after_ident;

                if (options.include_field_level_changes) {
                    TqPolicyDocument exp_doc;
                    exp_doc.routes.push_back(*before_route);
                    TqPolicyDocument act_doc;
                    act_doc.routes.push_back(after_route);

                    auto route_diff = compare_policy_ir(exp_doc, act_doc);
                    for (const auto& fd : route_diff.field_diffs) {
                        TqApplyPlanFieldChange fc;
                        fc.field_path = fd.field_path;
                        fc.before = fd.expected;
                        fc.after = fd.actual;
                        fc.impact = map_field_path_to_semantic_impact(fd.field_path);
                        route_change.field_changes.push_back(fc);
                    }
                }
                plan.route_changes.push_back(route_change);
            } else if (options.include_unchanged_routes) {
                // Unchanged route
                TqApplyPlanRouteChange route_change;
                route_change.route_id = r_id;
                route_change.kind = TqRouteChangeKind::Unchanged;
                route_change.before_identity = before_ident;
                route_change.after_identity = after_ident;
                plan.route_changes.push_back(route_change);
            }
        }
    }

    // Identify Removed routes
    for (const auto& before_route : before.routes) {
        std::string r_id = before_route.route_id;
        if (after_routes.find(r_id) == after_routes.end()) {
            TqApplyPlanRouteChange route_change;
            route_change.route_id = r_id;
            route_change.kind = TqRouteChangeKind::Removed;
            route_change.before_identity = compute_route_policy_identity(before_route);
            plan.route_changes.push_back(route_change);
        }
    }

    return plan;
}

std::string render_taperquery_apply_plan_markdown(const TqApplyPlan& plan) {
    std::stringstream ss;
    ss << "# 📋 TaperQuery Apply Dry-Run Report\n\n";

    ss << "## 🔍 Execution Summary\n";
    ss << "| Attribute | Value |\n";
    ss << "| --- | --- |\n";
    ss << "| **Status** | " << (plan.ok ? "🟢 APPROVED" : "🔴 BLOCKED") << " |\n";
    ss << "| **Active Content Hash** | `" << plan.before_policy_identity << "` |\n";
    ss << "| **Proposed Content Hash** | `" << plan.after_policy_identity << "` |\n";
    ss << "| **Expected Base Hash (CAS)** | `"
       << (plan.expected_base_identity.empty() ? "(none)" : plan.expected_base_identity)
       << "` |\n\n";

    // Route Changes
    ss << "## 🛣️ Route Actions & Declarations\n";
    if (plan.route_changes.empty()) {
        ss << "*No route differences detected between active and proposed policy.*\n\n";
    } else {
        ss << "| Route ID | Action | Before Identity | After Identity | Changes Count |\n";
        ss << "| --- | --- | --- | --- | --- |\n";
        for (const auto& rc : plan.route_changes) {
            std::string action_emoji = "🔹";
            if (rc.kind == TqRouteChangeKind::Added)
                action_emoji = "🟢 Added";
            else if (rc.kind == TqRouteChangeKind::Removed)
                action_emoji = "🔴 Removed";
            else if (rc.kind == TqRouteChangeKind::Modified)
                action_emoji = "🟡 Modified";
            else
                action_emoji = "⚪ Unchanged";

            ss << "| `" << rc.route_id << "` | " << action_emoji << " | "
               << (rc.before_identity.empty() ? "-" : "`" + rc.before_identity + "`") << " | "
               << (rc.after_identity.empty() ? "-" : "`" + rc.after_identity + "`") << " | "
               << rc.field_changes.size() << " |\n";
        }
        ss << "\n";

        // Field Level details
        bool has_field_details = false;
        for (const auto& rc : plan.route_changes) {
            if (!rc.field_changes.empty()) {
                has_field_details = true;
                break;
            }
        }

        if (has_field_details) {
            ss << "### 📐 Field-Level Semantic Diff Details\n";
            for (const auto& rc : plan.route_changes) {
                if (rc.field_changes.empty())
                    continue;
                ss << "#### 🛠️ Route: `" << rc.route_id << "`\n";
                ss << "| Field Path | Before | After | Semantic Impact |\n";
                ss << "| --- | --- | --- | --- |\n";
                for (const auto& fc : rc.field_changes) {
                    ss << "| `" << fc.field_path << "` | `" << fc.before << "` | `" << fc.after
                       << "` | **" << semantic_impact_to_string(fc.impact) << "** |\n";
                }
                ss << "\n";
            }
        }
    }

    // Issues section
    if (!plan.issues.empty()) {
        ss << "## ⚠️ Issue Diagnostics & Preconditions\n";
        for (const auto& issue : plan.issues) {
            std::string sev_emoji = "ℹ️ [INFO]";
            if (issue.severity == TqPlanSeverity::Blocker)
                sev_emoji = "❌ [BLOCKER]";
            else if (issue.severity == TqPlanSeverity::Warning)
                sev_emoji = "⚠️ [WARNING]";

            ss << "### " << sev_emoji << " Code: `" << issue.code << "`\n";
            if (!issue.route_id.empty()) {
                ss << "* **Target Route**: `" << issue.route_id << "`\n";
            }
            ss << "* **Reason**: " << issue.reason << "\n";
            ss << "* **Resolution Hint**: *" << issue.hint << "*\n\n";
        }
    }

    return ss.str();
}

std::string render_taperquery_apply_plan_text(const TqApplyPlan& plan) {
    std::stringstream ss;
    ss << "=== TAPERQUERY APPLY DRY-RUN REPORT ===\n\n";
    ss << "Status: " << (plan.ok ? "APPROVED" : "BLOCKED") << "\n";
    ss << "Active Content Hash: " << plan.before_policy_identity << "\n";
    ss << "Proposed Content Hash: " << plan.after_policy_identity << "\n";
    ss << "Expected Base Hash: "
       << (plan.expected_base_identity.empty() ? "(none)" : plan.expected_base_identity) << "\n\n";

    ss << "ROUTE CHANGES:\n";
    if (plan.route_changes.empty()) {
        ss << "  No route changes detected.\n";
    } else {
        for (const auto& rc : plan.route_changes) {
            ss << "  - Route ID: " << rc.route_id << " [" << route_change_kind_to_string(rc.kind)
               << "]\n";
            if (!rc.before_identity.empty())
                ss << "    Before Identity: " << rc.before_identity << "\n";
            if (!rc.after_identity.empty())
                ss << "    After Identity: " << rc.after_identity << "\n";
            if (!rc.field_changes.empty()) {
                ss << "    Field Diffs:\n";
                for (const auto& fc : rc.field_changes) {
                    ss << "      * " << fc.field_path << ": '" << fc.before << "' -> '" << fc.after
                       << "' (Semantic Impact: " << semantic_impact_to_string(fc.impact) << ")\n";
                }
            }
        }
    }
    ss << "\n";

    if (!plan.issues.empty()) {
        ss << "DIAGNOSTICS & ISSUES:\n";
        for (const auto& issue : plan.issues) {
            std::string sev_str = "INFO";
            if (issue.severity == TqPlanSeverity::Blocker)
                sev_str = "BLOCKER";
            else if (issue.severity == TqPlanSeverity::Warning)
                sev_str = "WARNING";

            ss << "  [" << sev_str << "] Code: " << issue.code << "\n";
            if (!issue.route_id.empty())
                ss << "    Route: " << issue.route_id << "\n";
            ss << "    Reason: " << issue.reason << "\n";
            ss << "    Hint: " << issue.hint << "\n\n";
        }
    }

    return ss.str();
}

// ============================================================================
// Legacy compatibility shim implementations
// ============================================================================

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
    TqPolicyValidationOptions val_opts{};
    val_opts.collect_all = true;
    val_opts.include_warnings = true;
    val_opts.strict_production = false;

    auto val_res = validate_taperquery_policy_ir(candidate_policy, val_opts);
    out->semantic_validation_ok = val_res.ok;

    for (const auto& issue : val_res.issues) {
        if (issue.severity == TqPolicyValidationSeverity::Error) {
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
            change.kind = TqLegacyRouteChangeKind::Added;
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
                change.kind = TqLegacyRouteChangeKind::Updated;

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
                change.kind = TqLegacyRouteChangeKind::Reordered;
                change.risk = TqRiskLevel::High;
                change.summary = "route reordered";
                out->route_changes.push_back(change);
            } else {
                if (options.include_unchanged_routes) {
                    change.kind = TqLegacyRouteChangeKind::Unchanged;
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
            change.kind = TqLegacyRouteChangeKind::Removed;
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
            std::string first_err;
            for (const auto& issue : val_res.issues) {
                if (issue.severity == TqPolicyValidationSeverity::Error) {
                    first_err = issue.reason;
                    break;
                }
            }
            out->message = std::string("Validation failed: ") + first_err;
        } else {
            std::string first_warn;
            for (const auto& issue : val_res.issues) {
                if (issue.severity == TqPolicyValidationSeverity::Warning) {
                    first_warn = issue.reason;
                    break;
                }
            }
            out->message =
                std::string("Validation warning (fail_on_validation_warning=true): ") + first_warn;
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

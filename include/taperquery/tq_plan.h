// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_TQ_PLAN_H
#define BYTETAPER_TAPERQUERY_TQ_PLAN_H

#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_compare.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

// ============================================================================
// GA-spec TqApplyPlan API & definitions (Partial-GA Compliance)
// ============================================================================

enum class TqPlanSeverity : std::uint8_t {
    Info,
    Warning,
    Blocker,
};

enum class TqRouteChangeKind : std::uint8_t {
    Added,
    Removed,
    Modified,
    Unchanged,
};

enum class TqSemanticImpact : std::uint8_t {
    None,
    MatchBehavior,
    MutationBehavior,
    CacheBehavior,
    CacheKeyBehavior,
    CacheStorageBehavior,
    FieldFilteringBehavior,
    PaginationBehavior,
    CompressionBehavior,
    CoalescingBehavior,
    FailureBehavior,
    RuntimeCompatibility,
};

struct TqApplyPlanFieldChange {
    std::string field_path;
    std::string before;
    std::string after;
    TqSemanticImpact impact = TqSemanticImpact::None;
};

struct TqApplyPlanRouteChange {
    std::string route_id;
    TqRouteChangeKind kind = TqRouteChangeKind::Unchanged;
    std::string before_identity;
    std::string after_identity;
    std::vector<TqApplyPlanFieldChange> field_changes;
};

struct TqApplyPlanIssue {
    TqPlanSeverity severity = TqPlanSeverity::Info;
    std::string code;
    std::string route_id;
    std::string reason;
    std::string hint;
};

struct TqApplyPlan {
    bool ok = true;
    std::string before_policy_identity;
    std::string after_policy_identity;
    std::string expected_base_identity;
    std::vector<TqApplyPlanRouteChange> route_changes;
    std::vector<TqApplyPlanIssue> issues;
};

struct TqApplyPlanOptions {
    bool include_unchanged_routes = false;
    bool include_field_level_changes = true;
    bool strict_production = true;
};

TqApplyPlan build_taperquery_apply_plan(const TqPolicyDocument& before,
                                        const TqPolicyDocument& after,
                                        const TqApplyPlanOptions& options = {});

std::string render_taperquery_apply_plan_markdown(const TqApplyPlan& plan);
std::string render_taperquery_apply_plan_text(const TqApplyPlan& plan);

// ============================================================================
// Legacy compatibility shim definitions & APIs
// ============================================================================

enum class TqPlanStatus : std::uint8_t {
    Ready = 0,
    BlockedByValidation = 1,
    BlockedByCasMismatch = 2,
    Error = 3,
};

enum class TqCasStatus : std::uint8_t {
    NotRequired = 0,
    Match = 1,
    Mismatch = 2,
    MissingExpectedBase = 3,
};

enum class TqLegacyRouteChangeKind : std::uint8_t {
    Unchanged = 0,
    Added = 1,
    Removed = 2,
    Updated = 3,
    Reordered = 4,
};

enum class TqRiskLevel : std::uint8_t {
    None = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Critical = 4,
};

struct TqRouteChange {
    TqLegacyRouteChangeKind kind = TqLegacyRouteChangeKind::Unchanged;
    std::string route_id;
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    bool order_changed = false;
    std::string before_identity;
    std::string after_identity;
    std::vector<PolicyIrFieldDiff> field_diffs;
    TqRiskLevel risk = TqRiskLevel::None;
    std::string summary;
};

struct TqRiskSummary {
    TqRiskLevel highest_risk = TqRiskLevel::None;
    std::uint32_t added_routes = 0;
    std::uint32_t removed_routes = 0;
    std::uint32_t updated_routes = 0;
    std::uint32_t reordered_routes = 0;
    std::uint32_t cache_changes = 0;
    std::uint32_t coalescing_changes = 0;
    std::uint32_t compression_changes = 0;
    std::uint32_t pagination_changes = 0;
    std::uint32_t mutation_changes = 0;
    std::uint32_t validation_errors = 0;
    std::uint32_t validation_warnings = 0;
};

struct TqPolicyChangePlan {
    TqPlanStatus status = TqPlanStatus::Error;
    TqCasStatus cas_status = TqCasStatus::NotRequired;

    std::string current_identity;
    std::string candidate_identity;
    std::string expected_base_identity;

    bool semantic_validation_ok = false;
    std::vector<TqRouteChange> route_changes;
    std::vector<PolicyIrFieldDiff> document_field_diffs;
    TqRiskSummary risk_summary;
    std::string message;
};

struct TqPlanOptions {
    bool require_cas = true;
    bool include_unchanged_routes = false;
    bool fail_on_validation_warning = false;
    bool route_order_sensitive = true;
};

bool build_taperquery_change_plan(const TqPolicyDocument& current_policy,
                                  const TqPolicyDocument& candidate_policy,
                                  const TqPlanOptions& options, TqPolicyChangePlan* out);

enum class TqApplyDecision : std::uint8_t {
    Allowed = 0,
    DeniedValidationFailed = 1,
    DeniedCasMismatch = 2,
    DeniedNoChanges = 3,
    DeniedPlanError = 4,
};

TqApplyDecision evaluate_apply_decision(const TqPolicyChangePlan& plan);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_TQ_PLAN_H

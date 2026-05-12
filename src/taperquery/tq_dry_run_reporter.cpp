// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_dry_run_reporter.h"

#include <string>

namespace bytetaper::taperquery {

namespace {

std::string risk_to_string(TqRiskLevel r) {
    switch (r) {
    case TqRiskLevel::Low:
        return "low";
    case TqRiskLevel::Medium:
        return "medium";
    case TqRiskLevel::High:
        return "high";
    case TqRiskLevel::Critical:
        return "critical";
    default:
        return "none";
    }
}

std::string cas_status_to_string(TqCasStatus c) {
    switch (c) {
    case TqCasStatus::Match:
        return "match";
    case TqCasStatus::Mismatch:
        return "mismatch";
    case TqCasStatus::MissingExpectedBase:
        return "missing_expected_base";
    default:
        return "not_required";
    }
}

} // namespace

std::string render_taperquery_dry_run_text(const TqPolicyChangePlan& plan) {
    std::string out;
    out += "TaperQuery Dry Run\n\n";

    out += "Current identity:   " +
           (plan.current_identity.empty() ? "<none>" : plan.current_identity) + "\n";
    out += "Candidate identity: " +
           (plan.candidate_identity.empty() ? "<none>" : plan.candidate_identity) + "\n";
    out += "Expected base:      " +
           (plan.expected_base_identity.empty() ? "<none>" : plan.expected_base_identity) + "\n";
    out += "CAS status:         " + cas_status_to_string(plan.cas_status) + "\n";
    out += "Validation:         " + std::string(plan.semantic_validation_ok ? "ok" : "failed") +
           "\n\n";

    out += "Summary:\n";
    out += "  Added routes:       " + std::to_string(plan.risk_summary.added_routes) + "\n";
    out += "  Removed routes:     " + std::to_string(plan.risk_summary.removed_routes) + "\n";
    out += "  Updated routes:     " + std::to_string(plan.risk_summary.updated_routes) + "\n";
    out += "  Reordered routes:   " + std::to_string(plan.risk_summary.reordered_routes) + "\n";
    out += "  Highest risk:       " + risk_to_string(plan.risk_summary.highest_risk) + "\n\n";

    out += "Route changes:\n";
    if (plan.route_changes.empty()) {
        out += "  No route changes\n";
    } else {
        for (const auto& rc : plan.route_changes) {
            if (rc.kind == TqLegacyRouteChangeKind::Added) {
                out += "  + " + rc.route_id + " [" + risk_to_string(rc.risk) + "]\n";
            } else if (rc.kind == TqLegacyRouteChangeKind::Removed) {
                out += "  - " + rc.route_id + " [" + risk_to_string(rc.risk) + "]\n";
            } else if (rc.kind == TqLegacyRouteChangeKind::Updated) {
                out += "  ~ " + rc.route_id + " [" + risk_to_string(rc.risk) + "]\n";
                if (rc.order_changed) {
                    out += "      route order changed\n";
                }
                for (const auto& fd : rc.field_diffs) {
                    out +=
                        "      " + fd.field_path + ": " + fd.expected + " -> " + fd.actual + "\n";
                }
            } else if (rc.kind == TqLegacyRouteChangeKind::Reordered) {
                out += "  ! " + rc.route_id + " [" + risk_to_string(rc.risk) + "]\n";
            }
        }
    }

    if (!plan.message.empty()) {
        out += "\nWarnings / errors:\n  " + plan.message + "\n";
    }

    return out;
}

} // namespace bytetaper::taperquery

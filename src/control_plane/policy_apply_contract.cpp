// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_apply_contract.h"

namespace bytetaper::control_plane {

PolicyRouteDiffPlan compute_route_diff_plan(const taperquery::TqApplyPlan& plan) {
    PolicyRouteDiffPlan summary{};
    for (const auto& change : plan.route_changes) {
        switch (change.kind) {
        case taperquery::TqRouteChangeKind::Added:
            summary.added_routes++;
            break;
        case taperquery::TqRouteChangeKind::Removed:
            summary.removed_routes++;
            break;
        case taperquery::TqRouteChangeKind::Modified:
            summary.modified_routes++;
            break;
        case taperquery::TqRouteChangeKind::Unchanged:
            summary.unchanged_routes++;
            break;
        }
    }
    return summary;
}

std::vector<PolicyApplyDiagnostic>
map_plan_issues_to_diagnostics(const taperquery::TqApplyPlan& plan) {
    std::vector<PolicyApplyDiagnostic> diagnostics;
    diagnostics.reserve(plan.issues.size());
    for (const auto& issue : plan.issues) {
        PolicyApplyDiagnostic diag;
        diag.severity = (issue.severity == taperquery::TqPlanSeverity::Blocker)   ? "error"
                        : (issue.severity == taperquery::TqPlanSeverity::Warning) ? "warning"
                                                                                  : "info";
        diag.code = issue.code;
        diag.route_id = issue.route_id;
        diag.message = issue.reason;
        if (!issue.hint.empty()) {
            if (!diag.message.empty()) {
                diag.message += " ";
            }
            diag.message += issue.hint;
        }
        diagnostics.push_back(std::move(diag));
    }
    return diagnostics;
}

} // namespace bytetaper::control_plane

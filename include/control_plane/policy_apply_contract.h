// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_APPLY_CONTRACT_H
#define BYTETAPER_CONTROL_PLANE_POLICY_APPLY_CONTRACT_H

#include "taperquery/tq_plan.h"

#include <string>
#include <vector>

namespace bytetaper::control_plane {

struct PolicyRouteDiffPlan {
    int added_routes = 0;
    int removed_routes = 0;
    int modified_routes = 0;
    int unchanged_routes = 0;
};

struct PolicyApplyDiagnostic {
    std::string severity;
    std::string code;
    std::string route_id;
    std::string message;
};

PolicyRouteDiffPlan compute_route_diff_plan(const taperquery::TqApplyPlan& plan);

std::vector<PolicyApplyDiagnostic>
map_plan_issues_to_diagnostics(const taperquery::TqApplyPlan& plan);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_APPLY_CONTRACT_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/policy_operational_diff.h"

namespace bytetaper::operational {

PolicyOperationalDiffResult
compute_policy_operational_diff(const taperquery::TqPolicyDocument& before,
                                const taperquery::TqPolicyDocument& after,
                                const taperquery::TqApplyPlanOptions& options) {
    PolicyOperationalDiffResult result{};
    result.plan = taperquery::build_taperquery_apply_plan(before, after, options);

    for (const auto& route_change : result.plan.route_changes) {
        if (route_change.kind != taperquery::TqRouteChangeKind::Unchanged) {
            result.affected_routes++;
        }
    }

    return result;
}

} // namespace bytetaper::operational

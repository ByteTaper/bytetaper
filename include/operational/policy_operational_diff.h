// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OPERATIONAL_POLICY_OPERATIONAL_DIFF_H
#define BYTETAPER_OPERATIONAL_POLICY_OPERATIONAL_DIFF_H

#include "taperquery/policy_ir.h"
#include "taperquery/tq_plan.h"

namespace bytetaper::operational {

struct PolicyOperationalDiffResult {
    taperquery::TqApplyPlan plan;
    std::uint32_t affected_routes = 0;
};

PolicyOperationalDiffResult
compute_policy_operational_diff(const taperquery::TqPolicyDocument& before,
                                const taperquery::TqPolicyDocument& after,
                                const taperquery::TqApplyPlanOptions& options = {});

} // namespace bytetaper::operational

#endif // BYTETAPER_OPERATIONAL_POLICY_OPERATIONAL_DIFF_H

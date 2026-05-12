// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_COMPARE_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_COMPARE_H

#include "taperquery/policy_ir.h"

#include <string>
#include <vector>

namespace bytetaper::taperquery {

struct PolicyIrFieldDiff {
    std::string route_id; // Holds the route_id, or "<document>" for top-level document mismatches
    std::string field_path;
    std::string expected;
    std::string actual;
    std::string hint;
};

struct PolicyIrDiff {
    bool equal = true;
    std::vector<PolicyIrFieldDiff> field_diffs;
};

/**
 * Recursively compares expected vs actual TqPolicyDocument and returns all
 * field-level differences with descriptive routes and paths.
 */
PolicyIrDiff compare_policy_ir(const TqPolicyDocument& expected, const TqPolicyDocument& actual);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_COMPARE_H

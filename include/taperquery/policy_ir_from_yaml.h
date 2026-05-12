// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_FROM_YAML_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_FROM_YAML_H

#include "policy/route_policy.h"
#include "taperquery/policy_ir.h"

#include <string>

namespace bytetaper::taperquery {

struct PolicyIrLoadResult {
    bool ok = false;
    TqPolicyDocument policy{};
    std::string error;
};

/**
 * Loads and constructs a modern TqPolicyDocument IR directly from a YAML file path.
 * Leverages the existing production yaml loader internally.
 */
PolicyIrLoadResult load_policy_ir_from_yaml_file(const char* path);

/**
 * Maps a single static C-array style RoutePolicy to our safe modern heap-based TqRoutePolicy IR.
 */
TqRoutePolicy from_runtime_route_policy(const policy::RoutePolicy& route);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_FROM_YAML_H

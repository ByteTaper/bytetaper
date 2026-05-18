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

PolicyIrLoadResult load_policy_ir_from_yaml_file(const char* path);

/**
 * Loads and constructs a modern TqPolicyDocument IR directly from a YAML content string.
 */
PolicyIrLoadResult load_policy_ir_from_yaml_string(const char* yaml_content, std::size_t len);

/**
 * Maps a single static C-array style RoutePolicy to our safe modern heap-based TqRoutePolicy IR.
 */
TqRoutePolicy from_runtime_route_policy(const policy::RoutePolicy& route);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_FROM_YAML_H

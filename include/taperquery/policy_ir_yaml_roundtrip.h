// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_YAML_ROUNDTRIP_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_YAML_ROUNDTRIP_H

#include "taperquery/policy_ir.h"

#include <functional>
#include <string>

namespace bytetaper::taperquery {

struct PolicyIrYamlRoundTripResult {
    bool ok = false;
    std::string canonical_yaml;
    TqPolicyDocument parsed_policy_ir;
    std::string candidate_policy_identity;
    std::string persisted_policy_identity;
    std::string error;
};

PolicyIrYamlRoundTripResult
emit_and_reparse_canonical_policy_yaml(const TqPolicyDocument& candidate_policy_ir);

void set_roundtrip_override_for_testing(
    std::function<PolicyIrYamlRoundTripResult(const TqPolicyDocument&)>* override_fn);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_YAML_ROUNDTRIP_H

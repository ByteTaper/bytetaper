// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_yaml_roundtrip.h"

#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_normalize.h"
#include "taperquery/policy_ir_yaml_emitter.h"

namespace bytetaper::taperquery {

namespace {
std::function<PolicyIrYamlRoundTripResult(const TqPolicyDocument&)>* g_roundtrip_override = nullptr;
} // namespace

void set_roundtrip_override_for_testing(
    std::function<PolicyIrYamlRoundTripResult(const TqPolicyDocument&)>* override_fn) {
    g_roundtrip_override = override_fn;
}

PolicyIrYamlRoundTripResult
emit_and_reparse_canonical_policy_yaml(const TqPolicyDocument& candidate_policy_ir) {
    if (g_roundtrip_override) {
        return (*g_roundtrip_override)(candidate_policy_ir);
    }
    PolicyIrYamlRoundTripResult result;

    // 1. Normalize candidate and compute its identity
    TqPolicyDocument normalized = normalize_policy_ir(candidate_policy_ir);
    result.candidate_policy_identity = compute_policy_document_identity(normalized);

    // 2. Emit canonical YAML
    auto emit_res = emit_policy_ir_canonical_yaml(normalized);
    if (!emit_res.ok) {
        result.error = "canonical YAML emit failed: " + emit_res.error;
        return result;
    }
    result.canonical_yaml = std::move(emit_res.yaml);

    // 3. Re-parse emitted YAML using the same loader path used at startup
    auto load_res = load_policy_ir_from_yaml_string(result.canonical_yaml.c_str(),
                                                    result.canonical_yaml.size());
    if (!load_res.ok) {
        result.error = "canonical YAML re-parse failed: " + load_res.error;
        return result;
    }

    // 4. Normalize parsed IR and compute persisted identity
    TqPolicyDocument parsed_normalized = normalize_policy_ir(load_res.policy);
    result.persisted_policy_identity = compute_policy_document_identity(parsed_normalized);
    result.parsed_policy_ir = std::move(parsed_normalized);
    result.ok = true;
    return result;
}

} // namespace bytetaper::taperquery

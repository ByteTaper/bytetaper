// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_YAML_EMITTER_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_YAML_EMITTER_H

#include "taperquery/policy_ir.h"

#include <string>

namespace bytetaper::taperquery {

struct PolicyIrYamlEmitOptions {
    bool include_header_comment = true;
    bool include_identity_comment = true;
};

struct PolicyIrYamlEmitResult {
    bool ok = false;
    std::string yaml;
    std::string error;
};

PolicyIrYamlEmitResult emit_policy_ir_canonical_yaml(const TqPolicyDocument& document,
                                                     const PolicyIrYamlEmitOptions& options = {});

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_YAML_EMITTER_H

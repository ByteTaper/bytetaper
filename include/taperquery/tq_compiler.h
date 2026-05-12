// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "taperquery/policy_ir.h"
#include "taperquery/tq_ast.h"
#include "taperquery/tq_diagnostic.h"

#include <cstdint>

namespace bytetaper::taperquery {

struct TqCompileOptions {
    bool allow_partial_document = false;
    bool preserve_source_order = true;
    bool strict_duplicate_blocks = true;
};

bool compile_taperquery_ast_to_policy_ir(const TqAstDocument& ast, const TqCompileOptions& options,
                                         TqPolicyDocument* out, TqDiagnosticBag* diagnostics);

} // namespace bytetaper::taperquery

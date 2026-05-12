// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "taperquery/tq_ast.h"
#include "taperquery/tq_diagnostic.h"

#include <cstddef>

namespace bytetaper::taperquery {

struct TqParseOptions {
    bool collect_all_diagnostics = true;
    bool allow_top_level_routes = true;
};

struct TqParseResult {
    bool ok = false;
    TqAstDocument document{};
    TqDiagnosticBag diagnostics{};
};

TqParseResult parse_taperquery_source(const char* source, std::size_t source_len,
                                      const TqParseOptions& options = {});

} // namespace bytetaper::taperquery

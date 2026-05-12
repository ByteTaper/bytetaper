// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "taperquery/tq_diagnostic.h"
#include "taperquery/tq_token.h"

#include <cstddef>

namespace bytetaper::taperquery {

class TqLexer {
public:
    TqLexer(const char* source, std::size_t len, TqDiagnosticBag* diagnostics = nullptr);

    TqToken next();

private:
    char peek_char() const;
    char advance_char();
    void skip_whitespace_and_comments();
    TqSourcePosition get_position() const {
        return position_;
    }

    TqToken lex_number();
    TqToken lex_string();
    TqToken lex_identifier();

    const char* source_;
    std::size_t len_;
    TqDiagnosticBag* diagnostics_;

    TqSourcePosition position_{};
};

} // namespace bytetaper::taperquery

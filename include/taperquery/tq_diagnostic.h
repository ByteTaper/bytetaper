// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "taperquery/tq_source_location.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace bytetaper::taperquery {

enum class TqDiagnosticSeverity : std::uint8_t {
    Error = 1,
    Warning = 2,
};

enum class TqDiagnosticCode : std::uint16_t {
    UnexpectedCharacter = 1,
    UnterminatedString = 2,
    InvalidNumber = 3,
    InvalidSizeLiteral = 4,
    InvalidDurationLiteral = 5,
    UnexpectedToken = 6,
    ExpectedIdentifier = 7,
    ExpectedString = 8,
    ExpectedBlock = 9,
    DuplicateClause = 10,
    ParserIssueLimitExceeded = 11,
};

struct TqDiagnostic {
    TqDiagnosticSeverity severity = TqDiagnosticSeverity::Error;
    TqDiagnosticCode code = TqDiagnosticCode::UnexpectedCharacter;
    TqSourceSpan span{};
    char message[128] = {};
};

static constexpr std::size_t kMaxTqDiagnostics = 128;

struct TqDiagnosticBag {
    std::size_t count = 0;
    TqDiagnostic diagnostics[kMaxTqDiagnostics] = {};
    bool truncated = false;
    bool collect_all = true;
};

// Allocation-free helper to add diagnostics to the bag
void report_tq_diagnostic_valist(TqDiagnosticBag* bag, TqDiagnosticSeverity severity,
                                 TqDiagnosticCode code, TqSourceSpan span, const char* message_fmt,
                                 std::va_list args);

void report_tq_diagnostic(TqDiagnosticBag* bag, TqDiagnosticSeverity severity,
                          TqDiagnosticCode code, TqSourceSpan span, const char* message_fmt, ...);

} // namespace bytetaper::taperquery

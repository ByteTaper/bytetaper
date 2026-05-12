// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_diagnostic.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace bytetaper::taperquery {

void report_tq_diagnostic_valist(TqDiagnosticBag* bag, TqDiagnosticSeverity severity,
                                 TqDiagnosticCode code, TqSourceSpan span, const char* message_fmt,
                                 std::va_list args) {
    if (!bag)
        return;

    if (bag->truncated)
        return;

    if (!bag->collect_all && bag->count >= 1) {
        bag->truncated = true;
        return;
    }

    if (bag->count >= kMaxTqDiagnostics) {
        // Overflow reached. Append the special ParserIssueLimitExceeded entry and mark truncated.
        bag->truncated = true;

        // Ensure we always have room for the limit-exceeded warning by overwriting the last slot if
        // full
        std::size_t slot = kMaxTqDiagnostics - 1;
        TqDiagnostic& diag = bag->diagnostics[slot];
        diag.severity = TqDiagnosticSeverity::Error;
        diag.code = TqDiagnosticCode::ParserIssueLimitExceeded;
        diag.span = span;
        std::strncpy(diag.message, "Parser issue limit exceeded; subsequent diagnostics truncated",
                     sizeof(diag.message) - 1);
        diag.message[sizeof(diag.message) - 1] = '\0';
        return;
    }

    TqDiagnostic& diag = bag->diagnostics[bag->count];
    diag.severity = severity;
    diag.code = code;
    diag.span = span;

    std::vsnprintf(diag.message, sizeof(diag.message), message_fmt, args);

    bag->count++;
}

void report_tq_diagnostic(TqDiagnosticBag* bag, TqDiagnosticSeverity severity,
                          TqDiagnosticCode code, TqSourceSpan span, const char* message_fmt, ...) {
    std::va_list args;
    va_start(args, message_fmt);
    report_tq_diagnostic_valist(bag, severity, code, span, message_fmt, args);
    va_end(args);
}

} // namespace bytetaper::taperquery

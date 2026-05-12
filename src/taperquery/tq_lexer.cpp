// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_lexer.h"

#include <cctype>
#include <cstring>

namespace bytetaper::taperquery {

namespace {

struct KeywordEntry {
    const char* name;
    TqTokenKind kind;
};

static constexpr KeywordEntry kKeywords[] = {
    { "policy", TqTokenKind::KeywordPolicy },
    { "route", TqTokenKind::KeywordRoute },
    { "when", TqTokenKind::KeywordWhen },
    { "path", TqTokenKind::KeywordPath },
    { "prefix", TqTokenKind::KeywordPrefix },
    { "exact", TqTokenKind::KeywordExact },
    { "method", TqTokenKind::KeywordMethod },
    { "and", TqTokenKind::KeywordAnd },
    { "against", TqTokenKind::KeywordAgainst },
    { "sha", TqTokenKind::KeywordSha },
    { "schema", TqTokenKind::KeywordSchema },
    { "mutate", TqTokenKind::KeywordMutate },
    { "failure", TqTokenKind::KeywordFailure },
    { "max_response", TqTokenKind::KeywordMaxResponse },
    { "cache", TqTokenKind::KeywordCache },
    { "store", TqTokenKind::KeywordStore },
    { "bypass", TqTokenKind::KeywordBypass },
    { "default", TqTokenKind::KeywordDefault },
    { "ttl", TqTokenKind::KeywordTtl },
    { "l1", TqTokenKind::KeywordL1 },
    { "l2", TqTokenKind::KeywordL2 },
    { "enabled", TqTokenKind::KeywordEnabled },
    { "capacity", TqTokenKind::KeywordCapacity },
    { "entries", TqTokenKind::KeywordEntries },
    { "private", TqTokenKind::KeywordPrivate },
    { "auth_scope_header", TqTokenKind::KeywordAuthScopeHeader },
    { "vary", TqTokenKind::KeywordVary },
    { "by", TqTokenKind::KeywordBy },
    { "headers", TqTokenKind::KeywordHeaders },
    { "field_variant", TqTokenKind::KeywordFieldVariant },
    { "max_variants_per_route", TqTokenKind::KeywordMaxVariantsPerRoute },
    { "min_field_count", TqTokenKind::KeywordMinFieldCount },
    { "max_field_count", TqTokenKind::KeywordMaxFieldCount },
    { "admission_threshold", TqTokenKind::KeywordAdmissionThreshold },
    { "ttl_max", TqTokenKind::KeywordTtlMax },
    { "fields", TqTokenKind::KeywordFields },
    { "allow", TqTokenKind::KeywordAllow },
    { "deny", TqTokenKind::KeywordDeny },
    { "paginate", TqTokenKind::KeywordPaginate },
    { "limit_offset", TqTokenKind::KeywordLimitOffset },
    { "cursor", TqTokenKind::KeywordCursor },
    { "limit_param", TqTokenKind::KeywordLimitParam },
    { "offset_param", TqTokenKind::KeywordOffsetParam },
    { "default_limit", TqTokenKind::KeywordDefaultLimit },
    { "max_limit", TqTokenKind::KeywordMaxLimit },
    { "upstream_supports_pagination", TqTokenKind::KeywordUpstreamSupportsPagination },
    { "max_response_bytes_warning", TqTokenKind::KeywordMaxResponseBytesWarning },
    { "compress", TqTokenKind::KeywordCompress },
    { "min_size", TqTokenKind::KeywordMinSize },
    { "eligible_content_types", TqTokenKind::KeywordEligibleContentTypes },
    { "prefer", TqTokenKind::KeywordPrefer },
    { "already_encoded", TqTokenKind::KeywordAlreadyEncoded },
    { "skip", TqTokenKind::KeywordSkip },
    { "passthrough", TqTokenKind::KeywordPassthrough },
    { "coalesce", TqTokenKind::KeywordCoalesce },
    { "cache_assisted", TqTokenKind::KeywordCacheAssisted },
    { "backend_timeout", TqTokenKind::KeywordBackendTimeout },
    { "handoff_buffer", TqTokenKind::KeywordHandoffBuffer },
    { "result_ready_retention", TqTokenKind::KeywordResultReadyRetention },
    { "max_waiters_per_key", TqTokenKind::KeywordMaxWaitersPerKey },
    { "require_cache", TqTokenKind::KeywordRequireCache },
    { "allow_authenticated", TqTokenKind::KeywordAllowAuthenticated },
    { "guardrails", TqTokenKind::KeywordGuardrails },
    { "max_follower_wait_budget", TqTokenKind::KeywordMaxFollowerWaitBudget },
    { "max_active_follower_waiters", TqTokenKind::KeywordMaxActiveFollowerWaiters },
    { "max_active_follower_waiters_per_shard",
      TqTokenKind::KeywordMaxActiveFollowerWaitersPerShard },
    { "fail_open", TqTokenKind::KeywordFailOpen },
    { "fail_closed", TqTokenKind::KeywordFailClosed },
    { "disabled", TqTokenKind::KeywordDisabled },
    { "headers_only", TqTokenKind::KeywordHeadersOnly },
    { "full", TqTokenKind::KeywordFull },
    { "any", TqTokenKind::KeywordAny },
    { "get", TqTokenKind::KeywordGet },
    { "post", TqTokenKind::KeywordPost },
    { "put", TqTokenKind::KeywordPut },
    { "delete", TqTokenKind::KeywordDelete },
    { "patch", TqTokenKind::KeywordPatch },
    { "true", TqTokenKind::KeywordTrue },
    { "false", TqTokenKind::KeywordFalse },
    { "gzip", TqTokenKind::KeywordGzip },
    { "brotli", TqTokenKind::KeywordBrotli },
    { "zstd", TqTokenKind::KeywordZstd },
    { "observe", TqTokenKind::KeywordObserve },
};

static constexpr std::size_t kKeywordCount = sizeof(kKeywords) / sizeof(kKeywords[0]);

TqTokenKind match_keyword(std::string_view ident) {
    for (std::size_t i = 0; i < kKeywordCount; ++i) {
        if (ident == kKeywords[i].name) {
            return kKeywords[i].kind;
        }
    }
    return TqTokenKind::Identifier;
}

} // namespace

TqLexer::TqLexer(const char* source, std::size_t len, TqDiagnosticBag* diagnostics)
    : source_(source), len_(len), diagnostics_(diagnostics) {
    position_.offset = 0;
    position_.line = 1;
    position_.column = 1;
}

char TqLexer::peek_char() const {
    if (position_.offset >= len_)
        return '\0';
    return source_[position_.offset];
}

char TqLexer::advance_char() {
    if (position_.offset >= len_)
        return '\0';
    char c = source_[position_.offset++];
    if (c == '\n') {
        position_.line++;
        position_.column = 1;
    } else {
        position_.column++;
    }
    return c;
}

void TqLexer::skip_whitespace_and_comments() {
    while (position_.offset < len_) {
        char c = peek_char();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance_char();
        } else if (c == '#' || (c == '/' && position_.offset + 1 < len_ &&
                                source_[position_.offset + 1] == '/')) {
            // Comment: skip to EOL
            while (position_.offset < len_ && peek_char() != '\n' && peek_char() != '\r') {
                advance_char();
            }
        } else {
            break;
        }
    }
}

TqToken TqLexer::next() {
    skip_whitespace_and_comments();

    TqSourcePosition start_pos = get_position();
    char c = peek_char();

    if (c == '\0') {
        TqToken tok{};
        tok.kind = TqTokenKind::EndOfFile;
        tok.span.start = start_pos;
        tok.span.end = start_pos;
        tok.text = "";
        return tok;
    }

    // Single character punctuation
    switch (c) {
    case '{':
        advance_char();
        return { TqTokenKind::LeftBrace,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case '}':
        advance_char();
        return { TqTokenKind::RightBrace,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case '[':
        advance_char();
        return { TqTokenKind::LeftBracket,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case ']':
        advance_char();
        return { TqTokenKind::RightBracket,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case '(':
        advance_char();
        return { TqTokenKind::LeftParen,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case ')':
        advance_char();
        return { TqTokenKind::RightParen,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case ',':
        advance_char();
        return { TqTokenKind::Comma,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case ':':
        advance_char();
        return { TqTokenKind::Colon,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case ';':
        advance_char();
        return { TqTokenKind::Semicolon,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case '.':
        advance_char();
        return { TqTokenKind::Dot,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    case '=':
        advance_char();
        return { TqTokenKind::Equal,
                 { start_pos, get_position() },
                 std::string_view(source_ + start_pos.offset, 1),
                 {} };
    default:
        break;
    }

    if (std::isdigit(static_cast<unsigned char>(c))) {
        return lex_number();
    }

    if (c == '"') {
        return lex_string();
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return lex_identifier();
    }

    // Invalid character
    advance_char();
    TqSourceSpan span{ start_pos, get_position() };
    if (diagnostics_) {
        report_tq_diagnostic(diagnostics_, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedCharacter, span,
                             "Unexpected character '%c'", c);
    }
    return { TqTokenKind::Invalid, span, std::string_view(source_ + start_pos.offset, 1), {} };
}

TqToken TqLexer::lex_number() {
    TqSourcePosition start_pos = get_position();
    std::uint64_t val = 0;
    bool overflow = false;

    while (position_.offset < len_ && std::isdigit(static_cast<unsigned char>(peek_char()))) {
        char digit = advance_char();
        if (!overflow) {
            std::uint64_t next_val = val * 10 + (digit - '0');
            if (next_val > 0xFFFFFFFFULL) {
                overflow = true;
                val = 0;
            } else {
                val = next_val;
            }
        }
    }

    TqSourcePosition suffix_start = get_position();
    TqToken tok{};
    tok.span.start = start_pos;

    if (overflow) {
        tok.kind = TqTokenKind::Invalid;
        tok.span.end = get_position();
        tok.text =
            std::string_view(source_ + start_pos.offset, tok.span.end.offset - start_pos.offset);
        if (diagnostics_) {
            report_tq_diagnostic(diagnostics_, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::InvalidNumber, tok.span,
                                 "Integer literal exceeds 32-bit limit");
        }
        return tok;
    }

    // Check suffixes
    char next_c = peek_char();
    if (std::isalpha(static_cast<unsigned char>(next_c))) {
        // Find suffix length
        std::size_t offset_before = position_.offset;
        while (position_.offset < len_ && std::isalpha(static_cast<unsigned char>(peek_char()))) {
            advance_char();
        }
        std::string_view suffix(source_ + offset_before, position_.offset - offset_before);
        tok.span.end = get_position();
        tok.text =
            std::string_view(source_ + start_pos.offset, tok.span.end.offset - start_pos.offset);

        if (suffix == "ms") {
            tok.kind = TqTokenKind::DurationLiteral;
            tok.duration_ms = static_cast<std::uint32_t>(val);
        } else if (suffix == "s") {
            if (val > 0xFFFFFFFFULL / 1000) {
                tok.kind = TqTokenKind::Invalid;
                if (diagnostics_) {
                    report_tq_diagnostic(
                        diagnostics_, TqDiagnosticSeverity::Error,
                        TqDiagnosticCode::InvalidDurationLiteral, tok.span,
                        "Duration literal in seconds overflows 32-bit milliseconds");
                }
            } else {
                tok.kind = TqTokenKind::DurationLiteral;
                tok.duration_ms = static_cast<std::uint32_t>(val * 1000);
            }
        } else if (suffix == "B") {
            tok.kind = TqTokenKind::SizeLiteral;
            tok.size_bytes = static_cast<std::uint32_t>(val);
        } else if (suffix == "KiB") {
            if (val > 0xFFFFFFFFULL / 1024) {
                tok.kind = TqTokenKind::Invalid;
                if (diagnostics_) {
                    report_tq_diagnostic(diagnostics_, TqDiagnosticSeverity::Error,
                                         TqDiagnosticCode::InvalidSizeLiteral, tok.span,
                                         "Size literal in KiB overflows 32-bit bytes");
                }
            } else {
                tok.kind = TqTokenKind::SizeLiteral;
                tok.size_bytes = static_cast<std::uint32_t>(val * 1024);
            }
        } else if (suffix == "MiB") {
            if (val > 0xFFFFFFFFULL / (1024 * 1024)) {
                tok.kind = TqTokenKind::Invalid;
                if (diagnostics_) {
                    report_tq_diagnostic(diagnostics_, TqDiagnosticSeverity::Error,
                                         TqDiagnosticCode::InvalidSizeLiteral, tok.span,
                                         "Size literal in MiB overflows 32-bit bytes");
                }
            } else {
                tok.kind = TqTokenKind::SizeLiteral;
                tok.size_bytes = static_cast<std::uint32_t>(val * 1024 * 1024);
            }
        } else {
            // Unknown suffix. Distinguish duration and size if we can, or report appropriately.
            // Suffix with 's' or 'm' suggests duration; else suggests size
            if (suffix.find('s') != std::string_view::npos ||
                suffix.find('m') != std::string_view::npos) {
                tok.kind = TqTokenKind::Invalid;
                if (diagnostics_) {
                    report_tq_diagnostic(diagnostics_, TqDiagnosticSeverity::Error,
                                         TqDiagnosticCode::InvalidDurationLiteral, tok.span,
                                         "Invalid duration suffix: %.*s",
                                         static_cast<int>(suffix.length()), suffix.data());
                }
            } else {
                tok.kind = TqTokenKind::Invalid;
                if (diagnostics_) {
                    report_tq_diagnostic(diagnostics_, TqDiagnosticSeverity::Error,
                                         TqDiagnosticCode::InvalidSizeLiteral, tok.span,
                                         "Invalid size suffix: %.*s",
                                         static_cast<int>(suffix.length()), suffix.data());
                }
            }
        }
    } else {
        // Simple integer
        tok.kind = TqTokenKind::IntegerLiteral;
        tok.int_val = static_cast<std::uint32_t>(val);
        tok.span.end = get_position();
        tok.text =
            std::string_view(source_ + start_pos.offset, tok.span.end.offset - start_pos.offset);
    }

    return tok;
}

TqToken TqLexer::lex_string() {
    TqSourcePosition start_pos = get_position();
    advance_char(); // skip opening quote '"'

    bool escaped = false;
    bool closed = false;

    while (position_.offset < len_) {
        char c = peek_char();
        if (c == '\n' || c == '\r') {
            break; // Unterminated on newline
        }
        advance_char();
        if (escaped) {
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            closed = true;
            break;
        }
    }

    TqToken tok{};
    tok.span.start = start_pos;
    tok.span.end = get_position();
    tok.text = std::string_view(source_ + start_pos.offset, tok.span.end.offset - start_pos.offset);

    if (!closed) {
        tok.kind = TqTokenKind::Invalid;
        if (diagnostics_) {
            report_tq_diagnostic(diagnostics_, TqDiagnosticSeverity::Error,
                                 TqDiagnosticCode::UnterminatedString, tok.span,
                                 "Unterminated string literal");
        }
    } else {
        tok.kind = TqTokenKind::StringLiteral;
    }

    return tok;
}

TqToken TqLexer::lex_identifier() {
    TqSourcePosition start_pos = get_position();
    while (position_.offset < len_) {
        char c = peek_char();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            advance_char();
        } else {
            break;
        }
    }

    TqToken tok{};
    tok.span.start = start_pos;
    tok.span.end = get_position();
    tok.text = std::string_view(source_ + start_pos.offset, tok.span.end.offset - start_pos.offset);
    tok.kind = match_keyword(tok.text);

    return tok;
}

} // namespace bytetaper::taperquery

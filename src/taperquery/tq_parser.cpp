// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_parser.h"

#include "taperquery/tq_lexer.h"

#include <cstdarg>
#include <cstring>

namespace bytetaper::taperquery {

namespace {

std::string decode_string_literal(std::string_view text) {
    if (text.length() < 2)
        return "";
    // Remove enclosing quotes
    std::string_view content = text.substr(1, text.length() - 2);
    std::string result;
    result.reserve(content.length());

    for (std::size_t i = 0; i < content.length(); ++i) {
        if (content[i] == '\\' && i + 1 < content.length()) {
            char next = content[++i];
            switch (next) {
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            default:
                result.push_back(next);
                break;
            }
        } else {
            result.push_back(content[i]);
        }
    }
    return result;
}

struct RouteBlockSeen {
    bool mutate = false;
    bool failure = false;
    bool max_response = false;
    bool cache = false;
    bool fields = false;
    bool paginate = false;
    bool compress = false;
    bool coalesce = false;
};

struct CacheBlockSeen {
    bool l1 = false;
    bool l2 = false;
    bool private_ = false;
    bool vary_headers = false;
    bool field_variant = false;
};

struct CoalescingBlockSeen {
    bool guardrails = false;
};

class ParserImpl {
public:
    ParserImpl(const char* source, std::size_t len, const TqParseOptions& options)
        : result_(), lexer_(source, len, &result_.diagnostics), options_(options) {
        result_.diagnostics.collect_all = options.collect_all_diagnostics;
        // Prime the pump
        curr_ = lexer_.next();
    }

    TqParseResult parse() {
        result_.ok = true;
        result_.document.span.start = curr_.span.start;

        while (curr_.kind != TqTokenKind::EndOfFile) {
            if (!options_.collect_all_diagnostics && result_.diagnostics.count >= 1) {
                result_.diagnostics.truncated = true;
                result_.ok = false;
                return result_;
            }
            if (curr_.kind == TqTokenKind::KeywordPolicy) {
                result_.document.policies.push_back(parse_policy_decl());
            } else if (curr_.kind == TqTokenKind::KeywordRoute) {
                if (!options_.allow_top_level_routes) {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Top level routes are disabled under current configuration");
                    result_.ok = false;
                    recover_to_statement();
                    continue;
                }
                result_.document.top_level_routes.push_back(parse_route_decl());
            } else if (curr_.kind == TqTokenKind::Invalid) {
                result_.ok = false;
                consume(); // skip invalid
            } else {
                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                             "Unexpected top-level token '%.*s'",
                             static_cast<int>(curr_.text.length()), curr_.text.data());
                result_.ok = false;
                recover_to_statement();
            }
        }

        result_.document.span.end = curr_.span.end;
        if (result_.diagnostics.count > 0) {
            // Check if any error was reported
            for (std::size_t i = 0; i < result_.diagnostics.count; ++i) {
                if (result_.diagnostics.diagnostics[i].severity == TqDiagnosticSeverity::Error) {
                    result_.ok = false;
                    break;
                }
            }
        }

        return result_;
    }

private:
    TqToken consume() {
        TqToken prev = curr_;
        curr_ = lexer_.next();
        return prev;
    }

    bool expect(TqTokenKind kind, const char* expected_msg,
                TqDiagnosticCode code = TqDiagnosticCode::UnexpectedToken) {
        if (curr_.kind == kind) {
            consume();
            return true;
        }
        report_error(code, curr_.span, "Expected %s, but found '%.*s'", expected_msg,
                     static_cast<int>(curr_.text.length()), curr_.text.data());
        result_.ok = false;
        return false;
    }

    void report_error(TqDiagnosticCode code, TqSourceSpan span, const char* fmt, ...) {
        std::va_list args;
        va_start(args, fmt);
        report_tq_diagnostic_valist(&result_.diagnostics, TqDiagnosticSeverity::Error, code, span,
                                    fmt, args);
        va_end(args);
    }

    void recover_to_statement() {
        while (curr_.kind != TqTokenKind::EndOfFile) {
            if (curr_.kind == TqTokenKind::Semicolon || curr_.kind == TqTokenKind::RightBrace) {
                consume();
                break;
            }
            if (curr_.kind == TqTokenKind::KeywordRoute ||
                curr_.kind == TqTokenKind::KeywordPolicy ||
                curr_.kind == TqTokenKind::KeywordMutate ||
                curr_.kind == TqTokenKind::KeywordFailure ||
                curr_.kind == TqTokenKind::KeywordMaxResponse ||
                curr_.kind == TqTokenKind::KeywordCache ||
                curr_.kind == TqTokenKind::KeywordFields ||
                curr_.kind == TqTokenKind::KeywordPaginate ||
                curr_.kind == TqTokenKind::KeywordCompress ||
                curr_.kind == TqTokenKind::KeywordCoalesce) {
                break;
            }
            consume();
        }
    }

    TqAstPolicyDecl parse_policy_decl() {
        TqAstPolicyDecl decl{};
        decl.span.start = curr_.span.start;

        consume(); // policy

        if (curr_.kind == TqTokenKind::StringLiteral) {
            decl.name = decode_string_literal(consume().text);
        } else {
            report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                         "Expected string literal for policy name");
            result_.ok = false;
        }

        // PolicyHeader?
        while (curr_.kind == TqTokenKind::KeywordSchema ||
               curr_.kind == TqTokenKind::KeywordAgainst) {
            if (curr_.kind == TqTokenKind::KeywordSchema) {
                consume(); // schema
                if (curr_.kind == TqTokenKind::StringLiteral) {
                    decl.schema = decode_string_literal(consume().text);
                } else {
                    report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                 "Expected schema string literal");
                    result_.ok = false;
                }
            } else {
                consume(); // against
                if (expect(TqTokenKind::KeywordSha, "sha")) {
                    if (curr_.kind == TqTokenKind::StringLiteral) {
                        decl.sha256 = decode_string_literal(consume().text);
                    } else {
                        report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                     "Expected sha string literal");
                        result_.ok = false;
                    }
                }
            }
        }

        if (expect(TqTokenKind::LeftBrace, "{", TqDiagnosticCode::ExpectedBlock)) {
            while (curr_.kind != TqTokenKind::RightBrace && curr_.kind != TqTokenKind::EndOfFile) {
                if (!options_.collect_all_diagnostics && result_.diagnostics.count >= 1) {
                    result_.diagnostics.truncated = true;
                    result_.ok = false;
                    return decl;
                }
                if (curr_.kind == TqTokenKind::KeywordRoute) {
                    decl.routes.push_back(parse_route_decl());
                } else {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Expected route inside policy declaration");
                    result_.ok = false;
                    recover_to_statement();
                }
            }
            decl.span.end = curr_.span.end;
            expect(TqTokenKind::RightBrace, "}");
        } else {
            decl.span.end = curr_.span.end;
        }

        return decl;
    }

    TqAstRouteDecl parse_route_decl() {
        TqAstRouteDecl decl{};
        decl.span.start = curr_.span.start;

        consume(); // route

        if (curr_.kind == TqTokenKind::StringLiteral) {
            decl.name = decode_string_literal(consume().text);
        } else {
            report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                         "Expected string literal for route name");
            result_.ok = false;
        }

        if (curr_.kind == TqTokenKind::KeywordWhen) {
            decl.has_when = true;
            decl.when_clause = parse_when_clause();
        }

        if (expect(TqTokenKind::LeftBrace, "{", TqDiagnosticCode::ExpectedBlock)) {
            RouteBlockSeen seen{};
            while (curr_.kind != TqTokenKind::RightBrace && curr_.kind != TqTokenKind::EndOfFile) {
                if (!options_.collect_all_diagnostics && result_.diagnostics.count >= 1) {
                    result_.diagnostics.truncated = true;
                    result_.ok = false;
                    return decl;
                }
                TqAstStatementKind kind = TqAstStatementKind::Mutate;
                bool is_valid_statement = true;

                if (curr_.kind == TqTokenKind::KeywordMutate) {
                    if (seen.mutate) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate mutate statement in route block");
                        result_.ok = false;
                    }
                    seen.mutate = true;
                    kind = TqAstStatementKind::Mutate;
                } else if (curr_.kind == TqTokenKind::KeywordFailure) {
                    if (seen.failure) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate failure statement in route block");
                        result_.ok = false;
                    }
                    seen.failure = true;
                    kind = TqAstStatementKind::Failure;
                } else if (curr_.kind == TqTokenKind::KeywordMaxResponse) {
                    if (seen.max_response) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate max_response statement in route block");
                        result_.ok = false;
                    }
                    seen.max_response = true;
                    kind = TqAstStatementKind::MaxResponse;
                } else if (curr_.kind == TqTokenKind::KeywordCache) {
                    if (seen.cache) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate cache statement in route block");
                        result_.ok = false;
                    }
                    seen.cache = true;
                    kind = TqAstStatementKind::Cache;
                } else if (curr_.kind == TqTokenKind::KeywordFields) {
                    if (seen.fields) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate fields statement in route block");
                        result_.ok = false;
                    }
                    seen.fields = true;
                    kind = TqAstStatementKind::Fields;
                } else if (curr_.kind == TqTokenKind::KeywordPaginate) {
                    if (seen.paginate) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate paginate statement in route block");
                        result_.ok = false;
                    }
                    seen.paginate = true;
                    kind = TqAstStatementKind::Paginate;
                } else if (curr_.kind == TqTokenKind::KeywordCompress) {
                    if (seen.compress) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate compress statement in route block");
                        result_.ok = false;
                    }
                    seen.compress = true;
                    kind = TqAstStatementKind::Compress;
                } else if (curr_.kind == TqTokenKind::KeywordCoalesce) {
                    if (seen.coalesce) {
                        report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                     "Duplicate coalesce statement in route block");
                        result_.ok = false;
                    }
                    seen.coalesce = true;
                    kind = TqAstStatementKind::Coalesce;
                } else if (curr_.kind == TqTokenKind::Semicolon) {
                    consume();
                    continue;
                } else {
                    is_valid_statement = false;
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Unexpected statement keyword '%.*s'",
                                 static_cast<int>(curr_.text.length()), curr_.text.data());
                    result_.ok = false;
                    recover_to_statement();
                }

                if (is_valid_statement) {
                    decl.statements.push_back(parse_statement(kind));
                }
            }
            decl.span.end = curr_.span.end;
            expect(TqTokenKind::RightBrace, "}");
        } else {
            decl.span.end = curr_.span.end;
        }

        return decl;
    }

    TqAstWhenClause parse_when_clause() {
        TqAstWhenClause clause{};
        clause.span.start = curr_.span.start;

        consume(); // when

        bool has_method = false;
        bool has_path = false;

        // Loop twice to allow alternative orders
        for (int i = 0; i < 2; ++i) {
            if (curr_.kind == TqTokenKind::KeywordMethod) {
                if (has_method) {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Duplicate method specification in when-clause");
                    result_.ok = false;
                }
                consume(); // method
                has_method = true;

                if (curr_.kind == TqTokenKind::KeywordAny) {
                    clause.match_expr.method = "any";
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordGet) {
                    clause.match_expr.method = "get";
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordPost) {
                    clause.match_expr.method = "post";
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordPut) {
                    clause.match_expr.method = "put";
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordDelete) {
                    clause.match_expr.method = "delete";
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordPatch) {
                    clause.match_expr.method = "patch";
                    consume();
                } else {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Expected HTTP method after method keyword");
                    result_.ok = false;
                }

                if (curr_.kind == TqTokenKind::KeywordAnd) {
                    consume(); // and
                }
            } else if (curr_.kind == TqTokenKind::KeywordPath) {
                if (has_path) {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Duplicate path specification in when-clause");
                    result_.ok = false;
                }
                consume(); // path
                has_path = true;

                if (curr_.kind == TqTokenKind::KeywordPrefix) {
                    clause.match_expr.path_kind = "prefix";
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordExact) {
                    clause.match_expr.path_kind = "exact";
                    consume();
                } else {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Expected prefix/exact after path keyword");
                    result_.ok = false;
                }

                if (curr_.kind == TqTokenKind::StringLiteral) {
                    clause.match_expr.path = decode_string_literal(consume().text);
                } else {
                    report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                 "Expected path string literal after matching kind");
                    result_.ok = false;
                }
            } else {
                break;
            }
        }

        if (!has_path) {
            report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                         "Expected path specification in when-clause");
            result_.ok = false;
        }

        clause.span.end = curr_.span.end;
        clause.match_expr.span = clause.span;
        return clause;
    }

    bool parse_boolean() {
        if (curr_.kind == TqTokenKind::KeywordTrue) {
            consume();
            return true;
        } else if (curr_.kind == TqTokenKind::KeywordFalse) {
            consume();
            return false;
        }
        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span, "Expected boolean true/false");
        result_.ok = false;
        return false;
    }

    TqAstStatement parse_statement(TqAstStatementKind kind) {
        TqAstStatement stmt{};
        stmt.kind = kind;
        stmt.span.start = curr_.span.start;

        consume(); // keyword

        if (kind == TqAstStatementKind::Mutate) {
            if (curr_.kind == TqTokenKind::KeywordDisabled) {
                stmt.mutate.mode = "disabled";
                consume();
            } else if (curr_.kind == TqTokenKind::KeywordHeadersOnly) {
                stmt.mutate.mode = "headers_only";
                consume();
            } else if (curr_.kind == TqTokenKind::KeywordFull) {
                stmt.mutate.mode = "full";
                consume();
            } else {
                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                             "Invalid mutate mode: '%.*s'", static_cast<int>(curr_.text.length()),
                             curr_.text.data());
                result_.ok = false;
            }
            stmt.mutate.span = { stmt.span.start, curr_.span.end };
            if (curr_.kind == TqTokenKind::Semicolon) {
                consume();
            }
        } else if (kind == TqAstStatementKind::Failure) {
            if (curr_.kind == TqTokenKind::KeywordFailOpen) {
                stmt.failure.mode = "fail_open";
                consume();
            } else if (curr_.kind == TqTokenKind::KeywordFailClosed) {
                stmt.failure.mode = "fail_closed";
                consume();
            } else {
                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                             "Invalid failure mode: '%.*s'", static_cast<int>(curr_.text.length()),
                             curr_.text.data());
                result_.ok = false;
            }
            stmt.failure.span = { stmt.span.start, curr_.span.end };
            if (curr_.kind == TqTokenKind::Semicolon) {
                consume();
            }
        } else if (kind == TqAstStatementKind::MaxResponse) {
            if (curr_.kind == TqTokenKind::SizeLiteral) {
                stmt.max_response.limit_bytes = curr_.size_bytes; // matches union
                consume();
            } else {
                report_error(TqDiagnosticCode::InvalidSizeLiteral, curr_.span,
                             "Expected response size limit as size literal");
                result_.ok = false;
            }
            stmt.max_response.span = { stmt.span.start, curr_.span.end };
            if (curr_.kind == TqTokenKind::Semicolon) {
                consume();
            }
        } else if (kind == TqAstStatementKind::Cache) {
            stmt.cache.enabled = true;
            if (curr_.kind == TqTokenKind::KeywordStore) {
                stmt.cache.behavior = "store";
                consume();
            } else if (curr_.kind == TqTokenKind::KeywordBypass) {
                stmt.cache.behavior = "bypass";
                consume();
            } else if (curr_.kind == TqTokenKind::KeywordDefault) {
                stmt.cache.behavior = "default";
                consume();
            }

            if (curr_.kind == TqTokenKind::KeywordTtl) {
                consume(); // ttl
                if (curr_.kind == TqTokenKind::DurationLiteral) {
                    stmt.cache.ttl_ms = curr_.duration_ms;
                    consume();
                } else {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Expected duration literal after ttl");
                    result_.ok = false;
                }
            }

            if (curr_.kind == TqTokenKind::LeftBrace) {
                consume(); // {
                CacheBlockSeen seen{};
                while (curr_.kind != TqTokenKind::RightBrace &&
                       curr_.kind != TqTokenKind::EndOfFile) {
                    if (curr_.kind == TqTokenKind::KeywordL1) {
                        if (seen.l1) {
                            report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                         "Duplicate L1 clause inside cache block");
                            result_.ok = false;
                        }
                        seen.l1 = true;
                        consume(); // l1
                        if (curr_.kind == TqTokenKind::KeywordEnabled) {
                            stmt.cache.l1.enabled = true;
                            consume();
                        }
                        if (curr_.kind == TqTokenKind::KeywordCapacity) {
                            consume(); // capacity
                            if (curr_.kind == TqTokenKind::IntegerLiteral) {
                                stmt.cache.l1.capacity_entries = curr_.int_val;
                                consume();
                            } else {
                                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                             "Expected integer capacity entries");
                                result_.ok = false;
                            }
                            if (curr_.kind == TqTokenKind::KeywordEntries) {
                                consume(); // entries
                            } else {
                                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                             "Expected entries keyword after capacity");
                                result_.ok = false;
                            }
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordL2) {
                        if (seen.l2) {
                            report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                         "Duplicate L2 clause inside cache block");
                            result_.ok = false;
                        }
                        seen.l2 = true;
                        consume(); // l2
                        if (curr_.kind == TqTokenKind::KeywordEnabled) {
                            stmt.cache.l2.enabled = true;
                            consume();
                        }
                        if (expect(TqTokenKind::KeywordPath, "path")) {
                            if (curr_.kind == TqTokenKind::StringLiteral) {
                                stmt.cache.l2.path = decode_string_literal(consume().text);
                            } else {
                                report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                             "Expected L2 path string literal");
                                result_.ok = false;
                            }
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordPrivate) {
                        if (seen.private_) {
                            report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                         "Duplicate private clause inside cache block");
                            result_.ok = false;
                        }
                        seen.private_ = true;
                        consume(); // private
                        if (expect(TqTokenKind::KeywordAuthScopeHeader, "auth_scope_header")) {
                            if (curr_.kind == TqTokenKind::StringLiteral) {
                                stmt.cache.private_cache.enabled = true;
                                stmt.cache.private_cache.auth_scope_header =
                                    decode_string_literal(consume().text);
                            } else {
                                report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                             "Expected auth scope header string literal");
                                result_.ok = false;
                            }
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordVary) {
                        if (seen.vary_headers) {
                            report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                         "Duplicate vary clause inside cache block");
                            result_.ok = false;
                        }
                        seen.vary_headers = true;
                        consume(); // vary
                        if (expect(TqTokenKind::KeywordBy, "by") &&
                            expect(TqTokenKind::KeywordHeaders, "headers")) {
                            stmt.cache.vary_headers.headers = parse_string_list();
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordFieldVariant) {
                        if (seen.field_variant) {
                            report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                         "Duplicate field_variant clause inside cache block");
                            result_.ok = false;
                        }
                        seen.field_variant = true;
                        consume(); // field_variant
                        if (expect(TqTokenKind::LeftBrace, "{")) {
                            while (curr_.kind != TqTokenKind::RightBrace &&
                                   curr_.kind != TqTokenKind::EndOfFile) {
                                if (curr_.kind == TqTokenKind::KeywordEnabled) {
                                    consume();
                                    stmt.cache.field_variant.enabled = parse_boolean();
                                } else if (curr_.kind == TqTokenKind::KeywordMaxVariantsPerRoute) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::IntegerLiteral) {
                                        stmt.cache.field_variant.max_variants_per_route =
                                            curr_.int_val;
                                        consume();
                                    } else {
                                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                     "Expected integer for max_variants_per_route");
                                        result_.ok = false;
                                    }
                                } else if (curr_.kind == TqTokenKind::KeywordMinFieldCount) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::IntegerLiteral) {
                                        stmt.cache.field_variant.min_field_count = curr_.int_val;
                                        consume();
                                    } else {
                                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                     "Expected integer for min_field_count");
                                        result_.ok = false;
                                    }
                                } else if (curr_.kind == TqTokenKind::KeywordMaxFieldCount) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::IntegerLiteral) {
                                        stmt.cache.field_variant.max_field_count = curr_.int_val;
                                        consume();
                                    } else {
                                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                     "Expected integer for max_field_count");
                                        result_.ok = false;
                                    }
                                } else if (curr_.kind == TqTokenKind::KeywordAdmissionThreshold) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::IntegerLiteral) {
                                        stmt.cache.field_variant.admission_threshold =
                                            curr_.int_val;
                                        consume();
                                    } else {
                                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                     "Expected integer for admission_threshold");
                                        result_.ok = false;
                                    }
                                } else if (curr_.kind == TqTokenKind::KeywordTtlMax) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::DurationLiteral) {
                                        stmt.cache.field_variant.ttl_max_ms = curr_.duration_ms;
                                        consume();
                                    } else {
                                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                     "Expected duration literal for ttl_max");
                                        result_.ok = false;
                                    }
                                } else {
                                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                 "Unexpected key in field_variant clause");
                                    result_.ok = false;
                                    recover_to_statement();
                                }
                            }
                            expect(TqTokenKind::RightBrace, "}");
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordEnabled) {
                        consume(); // enabled
                        stmt.cache.enabled = parse_boolean();
                    } else {
                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                     "Unexpected key in cache block");
                        result_.ok = false;
                        recover_to_statement();
                    }
                }
                expect(TqTokenKind::RightBrace, "}");
            } else {
                if (curr_.kind == TqTokenKind::Semicolon) {
                    consume();
                }
            }
            stmt.cache.span = { stmt.span.start, curr_.span.end };
        } else if (kind == TqAstStatementKind::Fields) {
            if (curr_.kind == TqTokenKind::KeywordAllow) {
                stmt.fields.mode = "allow";
                consume();
            } else if (curr_.kind == TqTokenKind::KeywordDeny) {
                stmt.fields.mode = "deny";
                consume();
            } else {
                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                             "Expected allow/deny keyword in fields statement");
                result_.ok = false;
            }
            stmt.fields.fields = parse_string_list();
            stmt.fields.span = { stmt.span.start, curr_.span.end };
            if (curr_.kind == TqTokenKind::Semicolon) {
                consume();
            }
        } else if (kind == TqAstStatementKind::Paginate) {
            stmt.paginate.enabled = true;
            if (curr_.kind == TqTokenKind::KeywordLimitOffset) {
                stmt.paginate.mode = "limit_offset";
                consume();
            } else if (curr_.kind == TqTokenKind::KeywordCursor) {
                stmt.paginate.mode = "cursor";
                consume();
            } else {
                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                             "Expected limit_offset/cursor pagination mode");
                result_.ok = false;
            }

            if (expect(TqTokenKind::LeftBrace, "{")) {
                while (curr_.kind != TqTokenKind::RightBrace &&
                       curr_.kind != TqTokenKind::EndOfFile) {
                    if (curr_.kind == TqTokenKind::KeywordLimitParam) {
                        consume();
                        if (curr_.kind == TqTokenKind::StringLiteral) {
                            stmt.paginate.limit_param = decode_string_literal(consume().text);
                        } else {
                            report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                         "Expected string limit parameter");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordOffsetParam) {
                        consume();
                        if (curr_.kind == TqTokenKind::StringLiteral) {
                            stmt.paginate.offset_param = decode_string_literal(consume().text);
                        } else {
                            report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                         "Expected string offset parameter");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordDefaultLimit) {
                        consume();
                        if (curr_.kind == TqTokenKind::IntegerLiteral) {
                            stmt.paginate.default_limit = curr_.int_val;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                         "Expected integer default limit");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordMaxLimit) {
                        consume();
                        if (curr_.kind == TqTokenKind::IntegerLiteral) {
                            stmt.paginate.max_limit = curr_.int_val;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                         "Expected integer max limit");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordUpstreamSupportsPagination) {
                        consume();
                        stmt.paginate.upstream_supports_pagination = parse_boolean();
                    } else if (curr_.kind == TqTokenKind::KeywordMaxResponseBytesWarning) {
                        consume();
                        if (curr_.kind == TqTokenKind::SizeLiteral) {
                            stmt.paginate.max_response_bytes_warning = curr_.size_bytes;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::InvalidSizeLiteral, curr_.span,
                                         "Expected size limit warning as size literal");
                            result_.ok = false;
                        }
                    } else {
                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                     "Unexpected key in paginate block");
                        result_.ok = false;
                        recover_to_statement();
                    }
                }
                expect(TqTokenKind::RightBrace, "}");
            }
            stmt.paginate.span = { stmt.span.start, curr_.span.end };
        } else if (kind == TqAstStatementKind::Compress) {
            stmt.compress.enabled = true;
            if (expect(TqTokenKind::LeftBrace, "{")) {
                while (curr_.kind != TqTokenKind::RightBrace &&
                       curr_.kind != TqTokenKind::EndOfFile) {
                    if (curr_.kind == TqTokenKind::KeywordEnabled) {
                        consume();
                        stmt.compress.enabled = parse_boolean();
                    } else if (curr_.kind == TqTokenKind::KeywordMinSize) {
                        consume();
                        if (curr_.kind == TqTokenKind::SizeLiteral) {
                            stmt.compress.min_size_bytes = curr_.size_bytes;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::InvalidSizeLiteral, curr_.span,
                                         "Expected size literal for min_size");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordEligibleContentTypes) {
                        consume();
                        stmt.compress.eligible_content_types = parse_string_list();
                    } else if (curr_.kind == TqTokenKind::KeywordPrefer) {
                        consume();
                        stmt.compress.preferred_algorithms = parse_algorithm_list();
                    } else if (curr_.kind == TqTokenKind::KeywordAlreadyEncoded) {
                        consume();
                        if (curr_.kind == TqTokenKind::KeywordSkip) {
                            stmt.compress.already_encoded_behavior = "skip";
                            consume();
                        } else if (curr_.kind == TqTokenKind::KeywordPassthrough) {
                            stmt.compress.already_encoded_behavior = "passthrough";
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                         "Expected skip/passthrough behavior");
                            result_.ok = false;
                        }
                    } else {
                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                     "Unexpected key in compress block");
                        result_.ok = false;
                        recover_to_statement();
                    }
                }
                expect(TqTokenKind::RightBrace, "}");
            }
            stmt.compress.span = { stmt.span.start, curr_.span.end };
        } else if (kind == TqAstStatementKind::Coalesce) {
            stmt.coalesce.enabled = true;
            if (curr_.kind == TqTokenKind::KeywordCacheAssisted) {
                stmt.coalesce.mode = "cache_assisted";
                consume();
            } else {
                report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                             "Expected cache_assisted coalescing mode");
                result_.ok = false;
            }

            if (expect(TqTokenKind::LeftBrace, "{")) {
                CoalescingBlockSeen seen{};
                while (curr_.kind != TqTokenKind::RightBrace &&
                       curr_.kind != TqTokenKind::EndOfFile) {
                    if (curr_.kind == TqTokenKind::KeywordEnabled) {
                        consume();
                        stmt.coalesce.enabled = parse_boolean();
                    } else if (curr_.kind == TqTokenKind::KeywordBackendTimeout) {
                        consume();
                        if (curr_.kind == TqTokenKind::DurationLiteral) {
                            stmt.coalesce.backend_timeout_ms = curr_.duration_ms;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                         "Expected duration literal for backend_timeout");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordHandoffBuffer) {
                        consume();
                        if (curr_.kind == TqTokenKind::DurationLiteral) {
                            stmt.coalesce.handoff_buffer_ms = curr_.duration_ms;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                         "Expected duration literal for handoff_buffer");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordResultReadyRetention) {
                        consume();
                        if (curr_.kind == TqTokenKind::DurationLiteral) {
                            stmt.coalesce.result_ready_retention_ms = curr_.duration_ms;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                         "Expected duration literal for result_ready_retention");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordMaxWaitersPerKey) {
                        consume();
                        if (curr_.kind == TqTokenKind::IntegerLiteral) {
                            stmt.coalesce.max_waiters_per_key = curr_.int_val;
                            consume();
                        } else {
                            report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                         "Expected integer max_waiters_per_key");
                            result_.ok = false;
                        }
                    } else if (curr_.kind == TqTokenKind::KeywordRequireCache) {
                        consume();
                        stmt.coalesce.require_cache_enabled = parse_boolean();
                    } else if (curr_.kind == TqTokenKind::KeywordAllowAuthenticated) {
                        consume();
                        stmt.coalesce.allow_authenticated = parse_boolean();
                    } else if (curr_.kind == TqTokenKind::KeywordGuardrails) {
                        if (seen.guardrails) {
                            report_error(TqDiagnosticCode::DuplicateClause, curr_.span,
                                         "Duplicate guardrails clause inside coalesce block");
                            result_.ok = false;
                        }
                        seen.guardrails = true;
                        consume(); // guardrails
                        stmt.coalesce.guardrails.enabled = true;
                        if (expect(TqTokenKind::LeftBrace, "{")) {
                            while (curr_.kind != TqTokenKind::RightBrace &&
                                   curr_.kind != TqTokenKind::EndOfFile) {
                                if (curr_.kind == TqTokenKind::KeywordMaxFollowerWaitBudget) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::DurationLiteral) {
                                        stmt.coalesce.guardrails.max_follower_wait_budget_ms =
                                            curr_.duration_ms;
                                        consume();
                                    } else {
                                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                     "Expected duration literal for "
                                                     "max_follower_wait_budget");
                                        result_.ok = false;
                                    }
                                } else if (curr_.kind ==
                                           TqTokenKind::KeywordMaxActiveFollowerWaiters) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::IntegerLiteral) {
                                        stmt.coalesce.guardrails.max_active_follower_waiters =
                                            curr_.int_val;
                                        consume();
                                    } else {
                                        report_error(
                                            TqDiagnosticCode::UnexpectedToken, curr_.span,
                                            "Expected integer max_active_follower_waiters");
                                        result_.ok = false;
                                    }
                                } else if (curr_.kind ==
                                           TqTokenKind::KeywordMaxActiveFollowerWaitersPerShard) {
                                    consume();
                                    if (curr_.kind == TqTokenKind::IntegerLiteral) {
                                        stmt.coalesce.guardrails
                                            .max_active_follower_waiters_per_shard = curr_.int_val;
                                        consume();
                                    } else {
                                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                     "Expected integer "
                                                     "max_active_follower_waiters_per_shard");
                                        result_.ok = false;
                                    }
                                } else {
                                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                                 "Unexpected key in guardrails clause");
                                    result_.ok = false;
                                    recover_to_statement();
                                }
                            }
                            expect(TqTokenKind::RightBrace, "}");
                        }
                    } else {
                        report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                     "Unexpected key in coalesce block");
                        result_.ok = false;
                        recover_to_statement();
                    }
                }
                expect(TqTokenKind::RightBrace, "}");
            }
            stmt.coalesce.span = { stmt.span.start, curr_.span.end };
        }

        return stmt;
    }

    std::vector<std::string> parse_string_list() {
        std::vector<std::string> list{};
        if (expect(TqTokenKind::LeftBracket, "[")) {
            while (curr_.kind != TqTokenKind::RightBracket &&
                   curr_.kind != TqTokenKind::EndOfFile) {
                if (curr_.kind == TqTokenKind::StringLiteral) {
                    list.push_back(decode_string_literal(consume().text));
                } else if (curr_.kind == TqTokenKind::Identifier) {
                    list.push_back(std::string(consume().text));
                } else {
                    report_error(TqDiagnosticCode::ExpectedString, curr_.span,
                                 "Expected string literal or identifier inside list");
                    result_.ok = false;
                    consume();
                }

                if (curr_.kind == TqTokenKind::Comma) {
                    consume();
                } else if (curr_.kind != TqTokenKind::RightBracket) {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Expected comma or right bracket inside list");
                    result_.ok = false;
                }
            }
            expect(TqTokenKind::RightBracket, "]");
        }
        return list;
    }

    std::vector<std::string> parse_algorithm_list() {
        std::vector<std::string> list{};
        if (expect(TqTokenKind::LeftBracket, "[")) {
            while (curr_.kind != TqTokenKind::RightBracket &&
                   curr_.kind != TqTokenKind::EndOfFile) {
                if (curr_.kind == TqTokenKind::KeywordGzip) {
                    list.push_back("gzip");
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordBrotli) {
                    list.push_back("brotli");
                    consume();
                } else if (curr_.kind == TqTokenKind::KeywordZstd) {
                    list.push_back("zstd");
                    consume();
                } else {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Expected algorithm enum inside compression list");
                    result_.ok = false;
                    consume();
                }

                if (curr_.kind == TqTokenKind::Comma) {
                    consume();
                } else if (curr_.kind != TqTokenKind::RightBracket) {
                    report_error(TqDiagnosticCode::UnexpectedToken, curr_.span,
                                 "Expected comma or right bracket inside list");
                    result_.ok = false;
                }
            }
            expect(TqTokenKind::RightBracket, "]");
        }
        return list;
    }

    TqParseResult result_{};
    TqLexer lexer_;
    TqParseOptions options_;
    TqToken curr_{};
};

} // namespace

TqParseResult parse_taperquery_source(const char* source, std::size_t source_len,
                                      const TqParseOptions& options) {
    TqParseResult result{};
    if (!source) {
        result.ok = false;
        TqSourceSpan span{};
        report_tq_diagnostic(&result.diagnostics, TqDiagnosticSeverity::Error,
                             TqDiagnosticCode::UnexpectedToken, span, "Null source buffer pointer");
        return result;
    }

    if (source_len == 0) {
        result.ok = true;
        return result;
    }

    ParserImpl parser(source, source_len, options);
    return parser.parse();
}

} // namespace bytetaper::taperquery

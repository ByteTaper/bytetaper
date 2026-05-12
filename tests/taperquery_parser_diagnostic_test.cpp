// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_parser.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::taperquery {

TEST(TqParserDiagnosticTest, ReportsUnexpectedToken) {
    const char* src = "policy \"p1\" { invalid_keyword_here }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::UnexpectedToken);
}

TEST(TqParserDiagnosticTest, ReportsExpectedStringAfterRoute) {
    const char* src = "route 12345 { }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::ExpectedString);
}

TEST(TqParserDiagnosticTest, ReportsExpectedPathString) {
    const char* src = "route \"r1\" when path prefix 12345 { }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::ExpectedString);
}

TEST(TqParserDiagnosticTest, ReportsExpectedBlock) {
    const char* src = "policy \"p1\" schema \"v1\" ;";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::ExpectedBlock);
}

TEST(TqParserDiagnosticTest, ReportsDuplicateMutateClause) {
    const char* src = "route \"r1\" { mutate full; mutate headers_only; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    bool found_duplicate = false;
    for (std::size_t i = 0; i < res.diagnostics.count; ++i) {
        if (res.diagnostics.diagnostics[i].code == TqDiagnosticCode::DuplicateClause) {
            found_duplicate = true;
            break;
        }
    }
    EXPECT_TRUE(found_duplicate);
}

TEST(TqParserDiagnosticTest, ReportsDuplicateCacheClause) {
    const char* src = "route \"r1\" { cache store ttl 60s; cache bypass; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    bool found_duplicate = false;
    for (std::size_t i = 0; i < res.diagnostics.count; ++i) {
        if (res.diagnostics.diagnostics[i].code == TqDiagnosticCode::DuplicateClause) {
            found_duplicate = true;
            break;
        }
    }
    EXPECT_TRUE(found_duplicate);
}

TEST(TqParserDiagnosticTest, ReportsInvalidSizeLiteral) {
    const char* src = "route \"r1\" { max_response 20GB; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    bool found_invalid_size = false;
    for (std::size_t i = 0; i < res.diagnostics.count; ++i) {
        if (res.diagnostics.diagnostics[i].code == TqDiagnosticCode::InvalidSizeLiteral) {
            found_invalid_size = true;
            break;
        }
    }
    EXPECT_TRUE(found_invalid_size);
}

TEST(TqParserDiagnosticTest, ReportsInvalidDurationLiteral) {
    const char* src = "route \"r1\" { cache store ttl 5minutes; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    bool found_invalid_duration = false;
    for (std::size_t i = 0; i < res.diagnostics.count; ++i) {
        if (res.diagnostics.diagnostics[i].code == TqDiagnosticCode::InvalidDurationLiteral) {
            found_invalid_duration = true;
            break;
        }
    }
    EXPECT_TRUE(found_invalid_duration);
}

TEST(TqParserDiagnosticTest, RecoversAfterInvalidStatement) {
    const char* src = "route \"r1\" { mutate invalid_mode; failure fail_open; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    TqAstRouteDecl r = res.document.top_level_routes[0];
    bool parsed_failure = false;
    for (const auto& stmt : r.statements) {
        if (stmt.kind == TqAstStatementKind::Failure && stmt.failure.mode == "fail_open") {
            parsed_failure = true;
            break;
        }
    }
    EXPECT_TRUE(parsed_failure);
}

TEST(TqParserDiagnosticTest, StopsAtDiagnosticLimit) {
    std::string src = "route \"r1\" {\n";
    for (int i = 0; i < 150; ++i) {
        src += "  invalid_statement_here;\n";
    }
    src += "}\n";

    TqParseResult res = parse_taperquery_source(src.c_str(), src.length());
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.diagnostics.count, 128u);
    EXPECT_TRUE(res.diagnostics.truncated);
    EXPECT_EQ(res.diagnostics.diagnostics[127].code, TqDiagnosticCode::ParserIssueLimitExceeded);
}

TEST(TqParserDiagnosticTest, StopsAfterFirstDiagnosticIfCollectAllIsFalse) {
    std::string src = "route \"r1\" {\n";
    for (int i = 0; i < 10; ++i) {
        src += "  invalid_statement_here;\n";
    }
    src += "}\n";

    TqParseOptions options;
    options.collect_all_diagnostics = false;

    TqParseResult res = parse_taperquery_source(src.c_str(), src.length(), options);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.diagnostics.count, 1u);
    EXPECT_TRUE(res.diagnostics.truncated);
}

TEST(TqParserDiagnosticTest, DetectsDurationConversionOverflow) {
    // 5000000s * 1000 = 5000000000 > UINT32_MAX
    const char* src = "route \"r1\" { cache store ttl 5000000s; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::InvalidDurationLiteral);
}

TEST(TqParserDiagnosticTest, DetectsSizeConversionOverflow) {
    // 4194304KiB * 1024 = 4294967296 > UINT32_MAX
    const char* src = "route \"r1\" { max_response 4194304KiB; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::InvalidSizeLiteral);
}

TEST(TqParserDiagnosticTest, DetectsNullSourcePointer) {
    TqParseResult res = parse_taperquery_source(nullptr, 0);
    EXPECT_FALSE(res.ok);
    ASSERT_EQ(res.diagnostics.count, 1u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::UnexpectedToken);
}

TEST(TqParserDiagnosticTest, RejectsBareIntegerForMaxResponse) {
    const char* src = "route \"r1\" { max_response 123; }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::InvalidSizeLiteral);
}

TEST(TqParserDiagnosticTest, RejectsBareIntegerForMaxResponseBytesWarning) {
    const char* src = "route \"r1\" { paginate limit_offset { max_response_bytes_warning 123 } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::InvalidSizeLiteral);
}

TEST(TqParserDiagnosticTest, RejectsBareIntegerForCompressMinSize) {
    const char* src = "route \"r1\" { compress { min_size 123 } }";
    TqParseResult res = parse_taperquery_source(src, std::strlen(src));
    EXPECT_FALSE(res.ok);
    ASSERT_GT(res.diagnostics.count, 0u);
    EXPECT_EQ(res.diagnostics.diagnostics[0].code, TqDiagnosticCode::InvalidSizeLiteral);
}

} // namespace bytetaper::taperquery

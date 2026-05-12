// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_lexer.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::taperquery {

TEST(TqLexerTest, LexesKeywords) {
    const char* src =
        "policy route when path prefix exact method and against sha schema mutate failure "
        "max_response cache store bypass default ttl l1 l2 enabled capacity entries private "
        "auth_scope_header vary by headers field_variant max_variants_per_route min_field_count "
        "max_field_count admission_threshold ttl_max fields allow deny paginate limit_offset "
        "cursor limit_param offset_param default_limit max_limit upstream_supports_pagination "
        "max_response_bytes_warning compress min_size eligible_content_types prefer "
        "already_encoded skip passthrough coalesce cache_assisted backend_timeout handoff_buffer "
        "result_ready_retention max_waiters_per_key require_cache allow_authenticated guardrails "
        "max_follower_wait_budget max_active_follower_waiters "
        "max_active_follower_waiters_per_shard fail_open fail_closed disabled headers_only full "
        "any get post put delete patch true false gzip brotli zstd";
    TqLexer lexer(src, std::strlen(src));

    TqTokenKind expected[] = {
        TqTokenKind::KeywordPolicy,
        TqTokenKind::KeywordRoute,
        TqTokenKind::KeywordWhen,
        TqTokenKind::KeywordPath,
        TqTokenKind::KeywordPrefix,
        TqTokenKind::KeywordExact,
        TqTokenKind::KeywordMethod,
        TqTokenKind::KeywordAnd,
        TqTokenKind::KeywordAgainst,
        TqTokenKind::KeywordSha,
        TqTokenKind::KeywordSchema,
        TqTokenKind::KeywordMutate,
        TqTokenKind::KeywordFailure,
        TqTokenKind::KeywordMaxResponse,
        TqTokenKind::KeywordCache,
        TqTokenKind::KeywordStore,
        TqTokenKind::KeywordBypass,
        TqTokenKind::KeywordDefault,
        TqTokenKind::KeywordTtl,
        TqTokenKind::KeywordL1,
        TqTokenKind::KeywordL2,
        TqTokenKind::KeywordEnabled,
        TqTokenKind::KeywordCapacity,
        TqTokenKind::KeywordEntries,
        TqTokenKind::KeywordPrivate,
        TqTokenKind::KeywordAuthScopeHeader,
        TqTokenKind::KeywordVary,
        TqTokenKind::KeywordBy,
        TqTokenKind::KeywordHeaders,
        TqTokenKind::KeywordFieldVariant,
        TqTokenKind::KeywordMaxVariantsPerRoute,
        TqTokenKind::KeywordMinFieldCount,
        TqTokenKind::KeywordMaxFieldCount,
        TqTokenKind::KeywordAdmissionThreshold,
        TqTokenKind::KeywordTtlMax,
        TqTokenKind::KeywordFields,
        TqTokenKind::KeywordAllow,
        TqTokenKind::KeywordDeny,
        TqTokenKind::KeywordPaginate,
        TqTokenKind::KeywordLimitOffset,
        TqTokenKind::KeywordCursor,
        TqTokenKind::KeywordLimitParam,
        TqTokenKind::KeywordOffsetParam,
        TqTokenKind::KeywordDefaultLimit,
        TqTokenKind::KeywordMaxLimit,
        TqTokenKind::KeywordUpstreamSupportsPagination,
        TqTokenKind::KeywordMaxResponseBytesWarning,
        TqTokenKind::KeywordCompress,
        TqTokenKind::KeywordMinSize,
        TqTokenKind::KeywordEligibleContentTypes,
        TqTokenKind::KeywordPrefer,
        TqTokenKind::KeywordAlreadyEncoded,
        TqTokenKind::KeywordSkip,
        TqTokenKind::KeywordPassthrough,
        TqTokenKind::KeywordCoalesce,
        TqTokenKind::KeywordCacheAssisted,
        TqTokenKind::KeywordBackendTimeout,
        TqTokenKind::KeywordHandoffBuffer,
        TqTokenKind::KeywordResultReadyRetention,
        TqTokenKind::KeywordMaxWaitersPerKey,
        TqTokenKind::KeywordRequireCache,
        TqTokenKind::KeywordAllowAuthenticated,
        TqTokenKind::KeywordGuardrails,
        TqTokenKind::KeywordMaxFollowerWaitBudget,
        TqTokenKind::KeywordMaxActiveFollowerWaiters,
        TqTokenKind::KeywordMaxActiveFollowerWaitersPerShard,
        TqTokenKind::KeywordFailOpen,
        TqTokenKind::KeywordFailClosed,
        TqTokenKind::KeywordDisabled,
        TqTokenKind::KeywordHeadersOnly,
        TqTokenKind::KeywordFull,
        TqTokenKind::KeywordAny,
        TqTokenKind::KeywordGet,
        TqTokenKind::KeywordPost,
        TqTokenKind::KeywordPut,
        TqTokenKind::KeywordDelete,
        TqTokenKind::KeywordPatch,
        TqTokenKind::KeywordTrue,
        TqTokenKind::KeywordFalse,
        TqTokenKind::KeywordGzip,
        TqTokenKind::KeywordBrotli,
        TqTokenKind::KeywordZstd,
    };

    for (std::size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        TqToken tok = lexer.next();
        EXPECT_EQ(tok.kind, expected[i])
            << "Failed at index " << i << " with word '" << tok.text << "'";
    }

    TqToken eof = lexer.next();
    EXPECT_EQ(eof.kind, TqTokenKind::EndOfFile);
}

TEST(TqLexerTest, LexesIdentifiers) {
    const char* src = "custom_identifier another123 _private";
    TqLexer lexer(src, std::strlen(src));

    TqToken t1 = lexer.next();
    EXPECT_EQ(t1.kind, TqTokenKind::Identifier);
    EXPECT_EQ(t1.text, "custom_identifier");

    TqToken t2 = lexer.next();
    EXPECT_EQ(t2.kind, TqTokenKind::Identifier);
    EXPECT_EQ(t2.text, "another123");

    TqToken t3 = lexer.next();
    EXPECT_EQ(t3.kind, TqTokenKind::Identifier);
    EXPECT_EQ(t3.text, "_private");
}

TEST(TqLexerTest, LexesStringLiteral) {
    const char* src = "\"simple string\"";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::StringLiteral);
    EXPECT_EQ(tok.text, "\"simple string\"");
}

TEST(TqLexerTest, LexesEscapedStringLiteral) {
    const char* src = "\"escaped \\\"quotes\\\" and \\\\ backslash\"";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::StringLiteral);
    EXPECT_EQ(tok.text, "\"escaped \\\"quotes\\\" and \\\\ backslash\"");
}

TEST(TqLexerTest, RejectsUnterminatedString) {
    const char* src = "\"unterminated";
    TqDiagnosticBag bag{};
    TqLexer lexer(src, std::strlen(src), &bag);

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::Invalid);
    EXPECT_EQ(bag.count, 1u);
    EXPECT_EQ(bag.diagnostics[0].code, TqDiagnosticCode::UnterminatedString);
}

TEST(TqLexerTest, LexesIntegerLiteral) {
    const char* src = "123456";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::IntegerLiteral);
    EXPECT_EQ(tok.int_val, 123456u);
}

TEST(TqLexerTest, LexesDurationMsLiteral) {
    const char* src = "500ms";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::DurationLiteral);
    EXPECT_EQ(tok.duration_ms, 500u);
}

TEST(TqLexerTest, LexesDurationSecondsLiteral) {
    const char* src = "5s";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::DurationLiteral);
    EXPECT_EQ(tok.duration_ms, 5000u);
}

TEST(TqLexerTest, LexesSizeBytesLiteral) {
    const char* src = "65536B";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::SizeLiteral);
    EXPECT_EQ(tok.size_bytes, 65536u);
}

TEST(TqLexerTest, LexesSizeKiBLiteral) {
    const char* src = "4KiB";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::SizeLiteral);
    EXPECT_EQ(tok.size_bytes, 4096u);
}

TEST(TqLexerTest, LexesSizeMiBLiteral) {
    const char* src = "2MiB";
    TqLexer lexer(src, std::strlen(src));

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::SizeLiteral);
    EXPECT_EQ(tok.size_bytes, 2097152u);
}

TEST(TqLexerTest, RejectsInvalidDurationUnit) {
    const char* src = "500minutes";
    TqDiagnosticBag bag{};
    TqLexer lexer(src, std::strlen(src), &bag);

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::Invalid);
    EXPECT_EQ(bag.count, 1u);
    EXPECT_EQ(bag.diagnostics[0].code, TqDiagnosticCode::InvalidDurationLiteral);
}

TEST(TqLexerTest, RejectsInvalidSizeUnit) {
    const char* src = "20GB";
    TqDiagnosticBag bag{};
    TqLexer lexer(src, std::strlen(src), &bag);

    TqToken tok = lexer.next();
    EXPECT_EQ(tok.kind, TqTokenKind::Invalid);
    EXPECT_EQ(bag.count, 1u);
    EXPECT_EQ(bag.diagnostics[0].code, TqDiagnosticCode::InvalidSizeLiteral);
}

TEST(TqLexerTest, TracksLineAndColumn) {
    const char* src = "route\n  \"api\"";
    TqLexer lexer(src, std::strlen(src));

    TqToken t1 = lexer.next();
    EXPECT_EQ(t1.kind, TqTokenKind::KeywordRoute);
    EXPECT_EQ(t1.span.start.line, 1u);
    EXPECT_EQ(t1.span.start.column, 1u);

    TqToken t2 = lexer.next();
    EXPECT_EQ(t2.kind, TqTokenKind::StringLiteral);
    EXPECT_EQ(t2.span.start.line, 2u);
    EXPECT_EQ(t2.span.start.column, 3u);
}

TEST(TqLexerTest, SkipsWhitespaceAndComments) {
    const char* src = "  # comment here\nroute // inline comment\n  policy";
    TqLexer lexer(src, std::strlen(src));

    TqToken t1 = lexer.next();
    EXPECT_EQ(t1.kind, TqTokenKind::KeywordRoute);
    EXPECT_EQ(t1.span.start.line, 2u);

    TqToken t2 = lexer.next();
    EXPECT_EQ(t2.kind, TqTokenKind::KeywordPolicy);
    EXPECT_EQ(t2.span.start.line, 3u);
}

} // namespace bytetaper::taperquery

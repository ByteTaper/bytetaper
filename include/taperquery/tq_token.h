// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "taperquery/tq_source_location.h"

#include <cstdint>
#include <string_view>

namespace bytetaper::taperquery {

enum class TqTokenKind : std::uint8_t {
    // Specials & Literals
    EndOfFile = 0,
    Invalid,
    Identifier,
    StringLiteral,
    IntegerLiteral,
    DurationLiteral,
    SizeLiteral,

    // Punctuation
    LeftBrace,    // {
    RightBrace,   // }
    LeftBracket,  // [
    RightBracket, // ]
    LeftParen,    // (
    RightParen,   // )
    Comma,        // ,
    Colon,        // :
    Semicolon,    // ;
    Dot,          // .
    Equal,        // =

    // Keywords
    KeywordPolicy,
    KeywordRoute,
    KeywordWhen,
    KeywordPath,
    KeywordPrefix,
    KeywordExact,
    KeywordMethod,
    KeywordAnd,
    KeywordAgainst,
    KeywordSha,
    KeywordSchema,
    KeywordMutate,
    KeywordFailure,
    KeywordMaxResponse,
    KeywordCache,
    KeywordStore,
    KeywordBypass,
    KeywordDefault,
    KeywordTtl,
    KeywordL1,
    KeywordL2,
    KeywordEnabled,
    KeywordCapacity,
    KeywordEntries,
    KeywordPrivate,
    KeywordAuthScopeHeader,
    KeywordVary,
    KeywordBy,
    KeywordHeaders,
    KeywordFieldVariant,
    KeywordMaxVariantsPerRoute,
    KeywordMinFieldCount,
    KeywordMaxFieldCount,
    KeywordAdmissionThreshold,
    KeywordTtlMax,
    KeywordFields,
    KeywordAllow,
    KeywordDeny,
    KeywordPaginate,
    KeywordLimitOffset,
    KeywordCursor,
    KeywordLimitParam,
    KeywordOffsetParam,
    KeywordDefaultLimit,
    KeywordMaxLimit,
    KeywordUpstreamSupportsPagination,
    KeywordMaxResponseBytesWarning,
    KeywordCompress,
    KeywordMinSize,
    KeywordEligibleContentTypes,
    KeywordPrefer,
    KeywordAlreadyEncoded,
    KeywordSkip,
    KeywordPassthrough,
    KeywordCoalesce,
    KeywordCacheAssisted,
    KeywordBackendTimeout,
    KeywordHandoffBuffer,
    KeywordResultReadyRetention,
    KeywordMaxWaitersPerKey,
    KeywordRequireCache,
    KeywordAllowAuthenticated,
    KeywordGuardrails,
    KeywordMaxFollowerWaitBudget,
    KeywordMaxActiveFollowerWaiters,
    KeywordMaxActiveFollowerWaitersPerShard,
    KeywordFailOpen,
    KeywordFailClosed,
    KeywordDisabled,
    KeywordHeadersOnly,
    KeywordFull,
    KeywordAny,
    KeywordGet,
    KeywordPost,
    KeywordPut,
    KeywordDelete,
    KeywordPatch,
    KeywordTrue,
    KeywordFalse,
    KeywordGzip,
    KeywordBrotli,
    KeywordZstd,
    KeywordObserve,
    KeywordInvalidation,
    KeywordOnMethods,
    KeywordTiming,
    KeywordAfterSuccessfulUpstreamResponse,
    KeywordSuccessStatus,
    KeywordMin,
    KeywordMax,
    KeywordTarget,
    KeywordRouteId,
    KeywordStrategy,
    KeywordRouteEpoch,
    KeywordExactKey,
};

struct TqToken {
    TqTokenKind kind = TqTokenKind::Invalid;
    TqSourceSpan span{};

    // Raw text matching this token
    std::string_view text{};

    // Decoded numeric literal storage
    union {
        std::uint32_t int_val = 0; // IntegerLiteral
        std::uint32_t duration_ms; // DurationLiteral
        std::uint32_t size_bytes;  // SizeLiteral
    };
};

} // namespace bytetaper::taperquery

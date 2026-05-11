// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once
#include <cstddef>
#include <cstdint>

namespace bytetaper::coalescing {

// Body-size contract — defines how ByteTaper classifies response bodies for caching and coalescing.
//
// Tier │ Condition                                              │ Behavior
// ─────┼────────────────────────────────────────────────────────┼────────────────────────────────────
//  0   │ body_len == 0                                          │ Not cacheable
//  1   │ body_len <= cache::kL1MaxBodySize (3 KiB)             │ L1 inline; followers receive
//  shared snapshot 2   │ body_len <= apg::ApgTransformContext::kL2BodyBufSize  │ L2 completion;
//  publish L2Ready handoff after store
//      │   (64 KiB)                                             │
//  3   │ body_len <= cache::kL2MaxBodySize (1 MiB)             │ Store to L2 for future probes; no
//  follower handoff
//      │                                                        │ (body exceeds follower
//      synchronous read buffer)
//  4   │ body_len > cache::kL2MaxBodySize                      │ Too large for L2; not stored
//
// Followers that time out while waiting for a tier-3 response may probe L2 on timeout
// but will receive L2GetResult::BodyTooLargeForBuffer, which is recorded distinctly
// from a cache miss (metric: follower_timeout_l2_body_too_large_total).
//
// See: decide_coalescing_completion_handoff() for the canonical classification.
//      cache::kL1MaxBodySize    — include/cache/l1_cache.h
//      apg::ApgTransformContext::kL2BodyBufSize — include/apg/context.h
//      cache::kL2MaxBodySize    — include/cache/l2_disk_cache.h

enum class CoalescingCompletionHandoffTarget : std::uint8_t {
    L1Inline = 0,
    L2Completion = 1,
    NotCacheable = 2,
    StoreToL2NoFollowerHandoff = 3, // store to L2, but body too large for follower L2 read buffer
    TooLargeForL2 = 4,              // body exceeds kL2MaxBodySize; do not store at all
};

CoalescingCompletionHandoffTarget decide_coalescing_completion_handoff(std::size_t body_len);

} // namespace bytetaper::coalescing

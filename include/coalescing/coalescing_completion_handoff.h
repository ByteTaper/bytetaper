// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once
#include <cstddef>
#include <cstdint>

namespace bytetaper::coalescing {

enum class CoalescingCompletionHandoffTarget : std::uint8_t {
    L1Inline = 0,
    L2Completion = 1,
    NotCacheable = 2,
    StoreToL2NoFollowerHandoff = 3, // store to L2, but body too large for follower L2 read buffer
    TooLargeForL2 = 4,              // body exceeds kL2MaxBodySize; do not store at all
};

CoalescingCompletionHandoffTarget decide_coalescing_completion_handoff(std::size_t body_len);

} // namespace bytetaper::coalescing

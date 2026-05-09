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
};

CoalescingCompletionHandoffTarget decide_coalescing_completion_handoff(std::size_t body_len);

} // namespace bytetaper::coalescing

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/coalescing_completion_handoff.h"

#include "cache/l1_cache.h"

namespace bytetaper::coalescing {

CoalescingCompletionHandoffTarget decide_coalescing_completion_handoff(std::size_t body_len) {
    if (body_len == 0) {
        return CoalescingCompletionHandoffTarget::NotCacheable;
    }
    if (body_len <= cache::kL1MaxBodySize) {
        return CoalescingCompletionHandoffTarget::L1Inline;
    }
    return CoalescingCompletionHandoffTarget::L2Completion;
}

} // namespace bytetaper::coalescing

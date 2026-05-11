// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/coalescing_completion_handoff.h"

#include "apg/context.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"

namespace bytetaper::coalescing {

// Body-size tier classification. See docs/runtime/BODY_SIZE_CONTRACT.md.
CoalescingCompletionHandoffTarget decide_coalescing_completion_handoff(std::size_t body_len) {
    if (body_len == 0) {
        return CoalescingCompletionHandoffTarget::NotCacheable;
    }
    if (body_len <= cache::kL1MaxBodySize) {
        return CoalescingCompletionHandoffTarget::L1Inline;
    }
    if (body_len <= apg::ApgTransformContext::kL2BodyBufSize) {
        return CoalescingCompletionHandoffTarget::L2Completion;
    }
    if (body_len <= cache::kL2MaxBodySize) {
        return CoalescingCompletionHandoffTarget::StoreToL2NoFollowerHandoff;
    }
    return CoalescingCompletionHandoffTarget::TooLargeForL2;
}

} // namespace bytetaper::coalescing

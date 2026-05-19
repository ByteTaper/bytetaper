// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/materialized_variant_sync.h"

namespace bytetaper::operational {

std::uint32_t
count_materialized_variant_invalidations(const taperquery::TqCacheNamespaceApplyResult& epoch_res) {
    std::uint32_t count = 0;
    for (const auto& route : epoch_res.routes) {
        if (route.variant_cleanup_required || route.l1_cleanup_required) {
            count++;
        }
    }
    return count;
}

} // namespace bytetaper::operational

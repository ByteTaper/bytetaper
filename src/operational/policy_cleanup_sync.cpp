// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/policy_cleanup_sync.h"

namespace bytetaper::operational {

PolicyCleanupSyncResult
prepare_operational_sync_for_apply(const taperquery::TqCacheNamespaceApplyResult& epoch_res,
                                   cache::L1Cache* l1_cache,
                                   taperquery::RouteCacheCleanupQueue* l2_cleanup_queue) {
    PolicyCleanupSyncResult result{};
    result.epoch_res =
        taperquery::prepare_operational_sync_for_apply(epoch_res, l1_cache, l2_cleanup_queue);

    for (const auto& route : result.epoch_res.routes) {
        if (route.l1_cleanup_required) {
            result.l1_cleanup_jobs++;
        }
        if (route.l2_cleanup_enqueued) {
            result.l2_cleanup_jobs++;
        }
    }

    return result;
}

} // namespace bytetaper::operational

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OPERATIONAL_POLICY_CLEANUP_SYNC_H
#define BYTETAPER_OPERATIONAL_POLICY_CLEANUP_SYNC_H

#include "cache/l1_cache.h"
#include "taperquery/tq_cache_namespace_versioning.h"

namespace bytetaper::operational {

struct PolicyCleanupSyncResult {
    taperquery::TqCacheNamespaceApplyResult epoch_res;
    std::uint32_t l1_cleanup_jobs = 0;
    std::uint32_t l2_cleanup_jobs = 0;
};

PolicyCleanupSyncResult
prepare_operational_sync_for_apply(const taperquery::TqCacheNamespaceApplyResult& epoch_res,
                                   cache::L1Cache* l1_cache,
                                   taperquery::RouteCacheCleanupQueue* l2_cleanup_queue);

} // namespace bytetaper::operational

#endif // BYTETAPER_OPERATIONAL_POLICY_CLEANUP_SYNC_H

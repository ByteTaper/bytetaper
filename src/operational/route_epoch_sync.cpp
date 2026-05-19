// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/route_epoch_sync.h"

namespace bytetaper::operational {

taperquery::TqCacheNamespaceApplyResult
sync_route_epochs_for_apply(const taperquery::TqApplyPlan& plan,
                            runtime::RouteCacheEpochStore* epoch_store) {
    return taperquery::version_epochs_for_apply(plan, epoch_store);
}

void rollback_route_epochs_for_apply(runtime::RouteCacheEpochStore* epoch_store,
                                     const taperquery::TqCacheNamespaceApplyResult& epoch_res) {
    if (epoch_store == nullptr) {
        return;
    }

    for (const auto& route : epoch_res.routes) {
        if (route.old_epoch == 0) {
            runtime::route_cache_epoch_remove(epoch_store, route.route_id.c_str());
        } else {
            runtime::route_cache_epoch_set(epoch_store, route.route_id.c_str(), route.old_epoch);
        }
    }
}

std::uint32_t count_bumped_route_epochs(const taperquery::TqCacheNamespaceApplyResult& epoch_res) {
    std::uint32_t count = 0;
    for (const auto& route : epoch_res.routes) {
        if (route.new_epoch != route.old_epoch) {
            count++;
        }
    }
    return count;
}

} // namespace bytetaper::operational

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OPERATIONAL_ROUTE_EPOCH_SYNC_H
#define BYTETAPER_OPERATIONAL_ROUTE_EPOCH_SYNC_H

#include "runtime/route_cache_epoch_store.h"
#include "taperquery/tq_cache_namespace_versioning.h"
#include "taperquery/tq_plan.h"

namespace bytetaper::operational {

taperquery::TqCacheNamespaceApplyResult
sync_route_epochs_for_apply(const taperquery::TqApplyPlan& plan,
                            runtime::RouteCacheEpochStore* epoch_store);

void rollback_route_epochs_for_apply(runtime::RouteCacheEpochStore* epoch_store,
                                     const taperquery::TqCacheNamespaceApplyResult& epoch_res);

std::uint32_t count_bumped_route_epochs(const taperquery::TqCacheNamespaceApplyResult& epoch_res);

} // namespace bytetaper::operational

#endif // BYTETAPER_OPERATIONAL_ROUTE_EPOCH_SYNC_H

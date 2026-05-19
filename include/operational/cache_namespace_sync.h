// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OPERATIONAL_CACHE_NAMESPACE_SYNC_H
#define BYTETAPER_OPERATIONAL_CACHE_NAMESPACE_SYNC_H

#include "taperquery/tq_cache_namespace_versioning.h"
#include "taperquery/tq_plan.h"

namespace bytetaper::operational {

taperquery::TqCacheNamespaceVersioningResult
detect_cache_namespace_impacts_for_plan(const taperquery::TqApplyPlan& plan,
                                        runtime::RouteCacheEpochStore* epoch_store);

} // namespace bytetaper::operational

#endif // BYTETAPER_OPERATIONAL_CACHE_NAMESPACE_SYNC_H

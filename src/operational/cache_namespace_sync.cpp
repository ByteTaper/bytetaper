// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "operational/cache_namespace_sync.h"

namespace bytetaper::operational {

taperquery::TqCacheNamespaceVersioningResult
detect_cache_namespace_impacts_for_plan(const taperquery::TqApplyPlan& plan,
                                        runtime::RouteCacheEpochStore* epoch_store) {
    return taperquery::detect_cache_namespace_impacts(plan, epoch_store);
}

} // namespace bytetaper::operational

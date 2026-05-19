// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OPERATIONAL_MATERIALIZED_VARIANT_SYNC_H
#define BYTETAPER_OPERATIONAL_MATERIALIZED_VARIANT_SYNC_H

#include "taperquery/tq_cache_namespace_versioning.h"

namespace bytetaper::operational {

// Materialized variants become unreachable via route epoch bump and L1 namespace cleanup
// (include_variant_entries=true). Returns count of routes requiring variant invalidation.
std::uint32_t
count_materialized_variant_invalidations(const taperquery::TqCacheNamespaceApplyResult& epoch_res);

} // namespace bytetaper::operational

#endif // BYTETAPER_OPERATIONAL_MATERIALIZED_VARIANT_SYNC_H

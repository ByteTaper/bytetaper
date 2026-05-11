// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/l1_variant_lookup_stage.h"

#include "cache/cache_key.h"
#include "policy/cache_policy.h"

#include <cstring>

namespace bytetaper::stages {

apg::StageOutput l1_variant_lookup_stage(apg::ApgTransformContext& context) {
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    if (context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
        return { apg::StageResult::Continue, "cache-disabled" };
    }

    if (!context.matched_policy->cache.field_variant.enabled) {
        return { apg::StageResult::Continue, "variant-policy-disabled" };
    }

    if (!context.variant_cache_key_ready) {
        return { apg::StageResult::Continue, "key-not-ready" };
    }

    const char* key_buf = context.variant_cache_key;

    // L1 lookup
    if (context.l1_cache == nullptr) {
        return { apg::StageResult::Continue, "no-l1-cache" };
    }

    cache::CacheEntry hit{};
    if (!cache::l1_get(context.l1_cache, key_buf, context.request_epoch_ms, &hit,
                       context.l2_body_buf, sizeof(context.l2_body_buf), context.cache_metrics)) {
        return { apg::StageResult::Continue, "l1-miss" };
    }

    // Hit - populate outputs
    context.cache_hit = true;
    context.cache_layer = "L1_VAR";
    context.should_return_immediate_response = true;
    context.cached_response = hit;

    return { apg::StageResult::SkipRemaining, "l1-variant-hit" };
}

} // namespace bytetaper::stages

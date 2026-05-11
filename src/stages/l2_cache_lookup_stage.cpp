// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/l2_cache_lookup_stage.h"

#include "cache/cache_key.h"
#include "cache/l2_disk_cache.h"
#include "metrics/runtime_metrics.h"
#include "policy/cache_policy.h"

namespace bytetaper::stages {

apg::StageOutput l2_cache_lookup_stage(apg::ApgTransformContext& context) {
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    if (context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
        return { apg::StageResult::Continue, "cache-disabled" };
    }

    if (context.l2_cache == nullptr) {
        return { apg::StageResult::Continue, "no-l2-cache" };
    }

    const char* key_buf = context.cache_key;
    bool is_variant = false;
    if (context.selected_field_count > 0 && context.matched_policy->cache.field_variant.enabled) {
        if (context.variant_cache_key_ready && context.variant_admission_passed) {
            key_buf = context.variant_cache_key;
            is_variant = true;
        } else {
            return { apg::StageResult::Continue, "has-query-selection-not-admitted-skip" };
        }
    } else if (!context.cache_key_ready) {
        return { apg::StageResult::Continue, "key-not-ready" };
    }

    // L2 lookup
    cache::CacheEntry hit{};
    cache::L2GetResult r =
        cache::l2_get_result(context.l2_cache, key_buf, context.request_epoch_ms, &hit,
                             context.l2_body_buf, apg::ApgTransformContext::kL2BodyBufSize);

    switch (r) {
    case cache::L2GetResult::Hit:
        break;
    case cache::L2GetResult::Miss:
        return { apg::StageResult::Continue, "l2-miss" };
    case cache::L2GetResult::Expired:
        metrics::record_runtime_event(context.runtime_metrics,
                                      metrics::RuntimeMetricEvent::L2LookupExpired);
        return { apg::StageResult::Continue, "l2-expired" };
    case cache::L2GetResult::DecodeError:
        metrics::record_runtime_event(context.runtime_metrics,
                                      metrics::RuntimeMetricEvent::L2LookupDecodeError);
        return { apg::StageResult::Continue, "l2-decode-error" };
    case cache::L2GetResult::BodyTooLargeForBuffer:
        metrics::record_runtime_event(context.runtime_metrics,
                                      metrics::RuntimeMetricEvent::L2LookupBodyTooLargeForBuffer);
        return { apg::StageResult::Continue, "l2-body-too-large-for-buffer" };
    case cache::L2GetResult::RocksDbError:
        metrics::record_runtime_event(context.runtime_metrics,
                                      metrics::RuntimeMetricEvent::L2LookupRocksDbError);
        return { apg::StageResult::Continue, "l2-error" };
    }

    // Hit - populate outputs
    if (context.l1_cache != nullptr) {
        cache::l1_put(context.l1_cache, hit, context.cache_metrics);
    }

    context.cache_hit = true;
    context.cache_layer = is_variant ? "L2_VAR" : "L2";
    context.should_return_immediate_response = true;
    context.cached_response = hit;

    return { apg::StageResult::SkipRemaining, is_variant ? "l2-variant-hit" : "l2-hit" };
}

} // namespace bytetaper::stages

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/l1_cache_store_stage.h"

#include "cache/cache_entry.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "metrics/cache_metrics.h"
#include "policy/cache_policy.h"

#include <cstring>

namespace bytetaper::stages {

apg::StageOutput l1_cache_store_stage(apg::ApgTransformContext& context) {
    metrics::record_cache_event(context.cache_metrics, metrics::CacheMetricEvent::L1StoreAttempt);

    if (context.selected_field_count > 0 && context.matched_policy != nullptr &&
        context.matched_policy->cache.field_variant.enabled) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "has-query-selection" };
    }

    if (context.matched_policy == nullptr) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "no-policy" };
    }

    if (context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "cache-disabled" };
    }

    if (context.request_method != policy::HttpMethod::Get) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "non-get" };
    }

    if (context.response_status_code < 200 || context.response_status_code >= 300) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "non-2xx" };
    }

    if (context.response_body == nullptr || context.response_body_len == 0) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "no-body" };
    }

    if (context.matched_policy->cache.ttl_seconds == 0) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "no-ttl" };
    }

    if (context.l1_cache == nullptr) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "no-l1-cache" };
    }

    if (context.response_body_len > cache::kL1MaxBodySize) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkippedBodyTooLarge);
        return { apg::StageResult::Continue, "body-too-large-for-l1" };
    }

    if (!context.cache_key_ready) {
        metrics::record_cache_event(context.cache_metrics,
                                    metrics::CacheMetricEvent::L1StoreSkipped);
        return { apg::StageResult::Continue, "key-not-ready" };
    }

    const char* key_buf = context.cache_key;

    // Build CacheEntry and store
    cache::CacheEntry entry{};
    std::strncpy(entry.key, key_buf, cache::kCacheKeyMaxLen - 1);
    entry.status_code = context.response_status_code;
    std::strncpy(entry.content_type, context.response_content_type,
                 cache::kCacheContentTypeMaxLen - 1);
    entry.body = context.response_body;
    entry.body_len = context.response_body_len;
    entry.original_body_len = context.input_payload_bytes;
    entry.removed_fields = static_cast<std::uint16_t>(context.removed_field_count);
    entry.created_at_epoch_ms = context.request_epoch_ms;
    entry.expires_at_epoch_ms =
        (context.request_epoch_ms > 0)
            ? context.request_epoch_ms +
                  static_cast<std::int64_t>(context.matched_policy->cache.ttl_seconds) * 1000
            : 0;

    cache::l1_put(context.l1_cache, entry);
    metrics::record_cache_event(context.cache_metrics, metrics::CacheMetricEvent::L1StoreSuccess);
    return { apg::StageResult::Continue, "stored" };
}

} // namespace bytetaper::stages

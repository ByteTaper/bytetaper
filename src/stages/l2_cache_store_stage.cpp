// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/l2_cache_store_stage.h"

#include "cache/cache_entry.h"
#include "cache/cache_key.h"
#include "cache/l2_disk_cache.h"
#include "policy/cache_policy.h"

#include <cstring>

namespace bytetaper::stages {

apg::StageOutput l2_cache_store_stage(apg::ApgTransformContext& context) {
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    if (context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
        return { apg::StageResult::Continue, "cache-disabled" };
    }

    if (context.request_method != policy::HttpMethod::Get) {
        return { apg::StageResult::Continue, "non-get" };
    }

    if (context.response_status_code < 200 || context.response_status_code >= 300) {
        return { apg::StageResult::Continue, "non-2xx" };
    }

    if (context.response_body == nullptr || context.response_body_len == 0) {
        return { apg::StageResult::Continue, "no-body" };
    }

    if (context.matched_policy->cache.ttl_seconds == 0) {
        return { apg::StageResult::Continue, "no-ttl" };
    }

    if (context.l2_cache == nullptr) {
        return { apg::StageResult::Continue, "no-l2-cache" };
    }

    const char* key_buf = context.cache_key;
    std::int64_t ttl_ms =
        static_cast<std::int64_t>(context.matched_policy->cache.ttl_seconds) * 1000;

    char key_buf_fallback[cache::kCacheKeyMaxLen] = {};
    if (!context.cache_key_ready) {
        cache::CacheKeyInput ki{};
        ki.method = context.request_method;
        ki.route_id = context.matched_policy->route_id;
        ki.path = context.raw_path;
        ki.query = context.raw_query;
        ki.selected_fields = context.selected_fields;
        ki.selected_field_count = context.selected_field_count;
        ki.policy_version = context.matched_policy->route_id;
        if (!cache::build_cache_key(ki, key_buf_fallback, sizeof(key_buf_fallback))) {
            return { apg::StageResult::Continue, "key-build-failed" };
        }
        key_buf = key_buf_fallback;
    } else if (context.selected_field_count > 0 &&
               context.matched_policy->cache.field_variant.enabled) {
        if (context.variant_cache_key_ready && context.variant_admission_passed) {
            key_buf = context.variant_cache_key;
            const auto& fv_policy = context.matched_policy->cache.field_variant;
            if (fv_policy.ttl_max_ms > 0 && ttl_ms > fv_policy.ttl_max_ms) {
                ttl_ms = fv_policy.ttl_max_ms;
            }
        } else {
            return { apg::StageResult::Continue, "has-query-selection-not-admitted-skip" };
        }
    }

    // Build CacheEntry and store
    cache::CacheEntry entry{};
    std::strncpy(entry.key, key_buf, cache::kCacheKeyMaxLen - 1);
    entry.status_code = context.response_status_code;
    std::strncpy(entry.content_type, context.response_content_type,
                 cache::kCacheContentTypeMaxLen - 1);
    entry.body = context.response_body;
    entry.body_len = context.response_body_len;
    entry.created_at_epoch_ms = context.request_epoch_ms;
    entry.expires_at_epoch_ms =
        (context.request_epoch_ms > 0) ? context.request_epoch_ms + ttl_ms : 0;

    if (cache::l2_put(context.l2_cache, entry)) {
        return { apg::StageResult::Continue, "stored" };
    } else {
        return { apg::StageResult::Continue, "store-failed" };
    }
}

} // namespace bytetaper::stages

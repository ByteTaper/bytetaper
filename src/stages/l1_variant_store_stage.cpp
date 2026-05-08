// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/l1_variant_store_stage.h"

#include "cache/cache_entry.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "policy/cache_policy.h"

#include <cstdio>
#include <cstring>

namespace bytetaper::stages {

apg::StageOutput l1_variant_store_stage(apg::ApgTransformContext& context) {
    std::printf("[L1 VAR STORE] Entering variant store stage...\n");
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    const auto& fv_policy = context.matched_policy->cache.field_variant;
    if (!fv_policy.enabled) {
        return { apg::StageResult::Continue, "variant-policy-disabled" };
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

    if (!context.variant_admission_passed) {
        return { apg::StageResult::Continue, "not-admitted" };
    }

    if (context.l1_cache == nullptr) {
        return { apg::StageResult::Continue, "no-l1-cache" };
    }

    if (context.response_body_len > cache::kL1MaxBodySize) {
        return { apg::StageResult::Continue, "body-too-large-for-l1" };
    }

    if (!context.variant_cache_key_ready) {
        return { apg::StageResult::Continue, "key-not-ready" };
    }

    const char* key_buf = context.variant_cache_key;

    // Calculate TTL (with field_variant.ttl_max_ms cap if positive)
    std::int64_t ttl_ms =
        static_cast<std::int64_t>(context.matched_policy->cache.ttl_seconds) * 1000;
    if (fv_policy.ttl_max_ms > 0 && ttl_ms > fv_policy.ttl_max_ms) {
        ttl_ms = fv_policy.ttl_max_ms;
    }

    if (ttl_ms <= 0) {
        return { apg::StageResult::Continue, "no-ttl" };
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

    cache::l1_put(context.l1_cache, entry);
    std::printf("[L1 VAR STORE] Successfully stored variant under key: %s\n", key_buf);
    return { apg::StageResult::Continue, "stored" };
}

} // namespace bytetaper::stages

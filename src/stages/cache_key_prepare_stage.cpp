// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/cache_key_prepare_stage.h"

#include "cache/cache_key.h"
#include "policy/cache_policy.h"

namespace bytetaper::stages {

apg::StageOutput cache_key_prepare_stage(apg::ApgTransformContext& context) {
    // 1. Reset state
    context.cache_eligible = false;
    context.cache_key_ready = false;
    context.variant_cache_key_ready = false;
    context.sanitized_query_ready = false;

    // 2. Pre-flight checks
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    if (context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
        return { apg::StageResult::Continue, "cache-disabled" };
    }

    if (context.request_method != policy::HttpMethod::Get) {
        return { apg::StageResult::Continue, "non-get" };
    }

    // 3. Mark as eligible
    context.cache_eligible = true;

    // 4. Sanitize query
    if (context.raw_query[0] != '\0') {
        cache::sanitize_query_strip_fields_param(context.raw_query, context.sanitized_query,
                                                 sizeof(context.sanitized_query));
        context.sanitized_query_ready = true;
    } else {
        context.sanitized_query[0] = '\0';
        context.sanitized_query_ready = true;
    }

    // 5. Build raw key (no fields segment if variant cache is enabled)
    cache::CacheKeyInput ki_raw{};
    ki_raw.method = context.request_method;
    ki_raw.route_id = context.matched_policy->route_id;
    ki_raw.path = context.raw_path;
    if (context.matched_policy->cache.field_variant.enabled) {
        ki_raw.query = context.sanitized_query_ready ? context.sanitized_query : context.raw_query;
        ki_raw.selected_fields = nullptr;
        ki_raw.selected_field_count = 0;
        ki_raw.variant = false;
    } else {
        ki_raw.query = context.raw_query;
        ki_raw.selected_fields = context.selected_fields;
        ki_raw.selected_field_count = context.selected_field_count;
        ki_raw.variant = false;
    }
    ki_raw.policy_version = context.matched_policy->route_id;

    if (cache::build_cache_key(ki_raw, context.cache_key, sizeof(context.cache_key))) {
        context.cache_key_ready = true;
    } else {
        return { apg::StageResult::Continue, "raw-key-build-failed" };
    }

    // 6. Build variant key (if variant policy is enabled)
    if (context.matched_policy->cache.field_variant.enabled) {
        cache::CacheKeyInput ki_var{};
        ki_var.method = context.request_method;
        ki_var.route_id = context.matched_policy->route_id;
        ki_var.path = context.raw_path;
        ki_var.query = context.sanitized_query_ready ? context.sanitized_query : context.raw_query;
        ki_var.selected_fields = context.selected_fields;
        ki_var.selected_field_count = context.selected_field_count;
        ki_var.policy_version = context.matched_policy->route_id;
        ki_var.variant = true;

        if (cache::build_cache_key(ki_var, context.variant_cache_key,
                                   sizeof(context.variant_cache_key))) {
            context.variant_cache_key_ready = true;
        }
    }

    std::printf("[PREPARE] cache_key_ready=%d key='%s' variant_ready=%d variant_key='%s'\n",
                context.cache_key_ready, context.cache_key, context.variant_cache_key_ready,
                context.variant_cache_key);

    return { apg::StageResult::Continue, "ready" };
}

} // namespace bytetaper::stages

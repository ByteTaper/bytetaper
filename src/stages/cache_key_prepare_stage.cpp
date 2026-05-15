// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/cache_key_prepare_stage.h"

#include "cache/cache_key.h"
#include "policy/cache_policy.h"
#include "runtime/route_cache_epoch_store.h"

namespace bytetaper::stages {

apg::StageOutput cache_key_prepare_stage(apg::ApgTransformContext& context) {
    // 1. Reset state
    context.cache_eligible = false;
    context.cache_key_ready = false;
    context.variant_cache_key_ready = false;
    context.sanitized_query_ready = false;
    context.route_cache_epoch_ready = false;

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

    if (context.cache_auth_bypass) {
        return { apg::StageResult::Continue, "auth-cache-bypass" };
    }

    // 2.5 Resolve Route Cache Epoch
    if (context.route_cache_epoch_store != nullptr) {
        std::uint64_t epoch = 0;
        const auto res = runtime::route_cache_epoch_get(context.route_cache_epoch_store,
                                                        context.matched_policy->route_id, &epoch);
        if (res == runtime::RouteCacheEpochResult::Ok) {
            context.route_cache_epoch = epoch;
            context.route_cache_epoch_ready = true;
        } else {
            // Epoch store present but route not registered → bypass cache
            // Not an error, just a safe skip.
            return { apg::StageResult::Continue, "route-epoch-not-ready" };
        }
    } else {
        // Epoch store missing for cacheable route → bypass cache
        return { apg::StageResult::Continue, "route-epoch-not-ready" };
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
    // Computed once — shared across all CacheKeyInputs below
    const char* policy_version = context.matched_policy->policy_identity[0] != '\0'
                                     ? context.matched_policy->policy_identity
                                     : context.matched_policy->route_id;

    const char* effective_query =
        context.sanitized_query_ready ? context.sanitized_query : context.raw_query;

    const bool pol_private = context.matched_policy->cache.private_cache;
    const char* auth_scope = nullptr;
    if (pol_private) {
        if (!context.private_cache_scope_ready) {
            return { apg::StageResult::Continue, "private-scope-not-ready" };
        }
        auth_scope = context.private_cache_scope_hash;
    }

    const std::size_t vary_count = context.cache_vary_ready ? context.cache_vary_count : 0;

    cache::CacheKeyInput ki_raw{};
    ki_raw.method = context.request_method;
    ki_raw.route_id = context.matched_policy->route_id;
    ki_raw.path = context.raw_path;
    if (context.matched_policy->cache.field_variant.enabled) {
        ki_raw.query = effective_query;
        ki_raw.selected_fields = nullptr;
        ki_raw.selected_field_count = 0;
        ki_raw.variant = false;
    } else {
        ki_raw.query = context.raw_query;
        ki_raw.selected_fields = context.selected_fields;
        ki_raw.selected_field_count = context.selected_field_count;
        ki_raw.variant = false;
    }
    ki_raw.policy_version = policy_version;
    ki_raw.route_cache_epoch = context.route_cache_epoch;
    ki_raw.route_cache_epoch_ready = context.route_cache_epoch_ready;
    ki_raw.private_cache = pol_private;
    ki_raw.auth_scope = auth_scope;

    for (std::size_t i = 0; i < vary_count; ++i) {
        ki_raw.vary_headers[i] = { context.cache_vary_names[i],
                                   context.cache_vary_value_hashes[i] };
    }
    ki_raw.vary_header_count = vary_count;

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
        ki_var.query = effective_query;
        ki_var.selected_fields = context.selected_fields;
        ki_var.selected_field_count = context.selected_field_count;
        ki_var.policy_version = policy_version;
        ki_var.route_cache_epoch = context.route_cache_epoch;
        ki_var.route_cache_epoch_ready = context.route_cache_epoch_ready;
        ki_var.variant = true;
        ki_var.private_cache = pol_private;
        ki_var.auth_scope = auth_scope;

        for (std::size_t i = 0; i < vary_count; ++i) {
            ki_var.vary_headers[i] = { context.cache_vary_names[i],
                                       context.cache_vary_value_hashes[i] };
        }
        ki_var.vary_header_count = vary_count;

        if (cache::build_cache_key(ki_var, context.variant_cache_key,
                                   sizeof(context.variant_cache_key))) {
            context.variant_cache_key_ready = true;
        }
    }

    return { apg::StageResult::Continue, "ready" };
}

} // namespace bytetaper::stages

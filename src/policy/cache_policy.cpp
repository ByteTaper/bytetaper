// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/cache_policy.h"

namespace bytetaper::policy {

const char* validate_cache_policy(const CachePolicy& policy) {
    if (policy.invalidation.enabled) {
        if (!policy.invalidation.on_patch && !policy.invalidation.on_put &&
            !policy.invalidation.on_delete) {
            return "cache.invalidation requires at least one method in on_methods";
        }
        if (policy.invalidation.target_count == 0) {
            return "cache.invalidation requires at least one target";
        }
        if (policy.invalidation.target_count > kMaxCacheInvalidationTargets) {
            return "cache.invalidation target count exceeds maximum allowed";
        }
        if (policy.invalidation.success_status_min < 100) {
            return "cache.invalidation success_status min must be >= 100";
        }
        if (policy.invalidation.success_status_max > 599) {
            return "cache.invalidation success_status max must be <= 599";
        }
        if (policy.invalidation.success_status_min > policy.invalidation.success_status_max) {
            return "cache.invalidation success_status min cannot be greater than max";
        }
        for (std::size_t i = 0; i < policy.invalidation.target_count; ++i) {
            if (policy.invalidation.targets[i].route_id[0] == '\0') {
                return "cache.invalidation target route_id must not be empty";
            }
        }
        if (policy.enabled && policy.behavior == CacheBehavior::Store) {
            return "route cannot declare both cache store and cache invalidation";
        }
    }

    if (!policy.enabled) {
        return nullptr; // disabled is always valid for standard cache
    }
    if (policy.ttl_seconds == 0) {
        return "cache.ttl_seconds required when cache is enabled";
    }
    if (!policy.l1.enabled && !policy.l2.enabled) {
        return "at least one cache layer (l1 or l2) must be enabled";
    }
    if (policy.l2.enabled && policy.l2.path[0] == '\0') {
        return "cache.layers.l2.path required when L2 cache is enabled";
    }
    return nullptr;
}

} // namespace bytetaper::policy

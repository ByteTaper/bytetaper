// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_invalidation_target_resolver.h"

#include <cstring>

namespace bytetaper::cache {

const policy::RoutePolicy* find_route_by_id(const policy::RoutePolicy* routes,
                                            std::size_t route_count, const char* route_id) {
    if (routes == nullptr || route_id == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < route_count; ++i) {
        if (routes[i].route_id == nullptr || routes[i].route_id[0] == '\0') {
            continue;
        }
        if (std::strcmp(routes[i].route_id, route_id) == 0) {
            return &routes[i];
        }
    }
    return nullptr;
}

static bool invalidation_policy_allows_method(const policy::CacheInvalidationPolicy& inv,
                                              policy::HttpMethod method) {
    if (method == policy::HttpMethod::Patch)
        return inv.on_patch;
    if (method == policy::HttpMethod::Put)
        return inv.on_put;
    if (method == policy::HttpMethod::Delete)
        return inv.on_delete;
    return false;
}

InvalidationTargetResolveResult
resolve_invalidation_targets(const policy::RoutePolicy* routes, std::size_t route_count,
                             const policy::RoutePolicy* mutation_route,
                             policy::HttpMethod request_method) {

    InvalidationTargetResolveResult result{};

    if (routes == nullptr || mutation_route == nullptr) {
        result.status = InvalidationTargetResolveStatus::InvalidArgument;
        result.reason = "invalid-argument";
        return result;
    }

    const auto& inv = mutation_route->cache.invalidation;

    if (!inv.enabled) {
        result.status = InvalidationTargetResolveStatus::Disabled;
        result.reason = "no-invalidation-policy";
        return result;
    }

    if (request_method != policy::HttpMethod::Patch && request_method != policy::HttpMethod::Put &&
        request_method != policy::HttpMethod::Delete) {
        result.status = InvalidationTargetResolveStatus::NonMutationMethod;
        result.reason = "non-mutation-method";
        return result;
    }

    if (!invalidation_policy_allows_method(inv, request_method)) {
        result.status = InvalidationTargetResolveStatus::MethodNotEnabled;
        result.reason = "method-not-enabled";
        return result;
    }

    if (inv.target_count == 0) {
        result.status = InvalidationTargetResolveStatus::NoTargets;
        result.reason = "no-targets";
        return result;
    }

    if (inv.target_count > kMaxResolvedInvalidationTargets) {
        result.status = InvalidationTargetResolveStatus::TooManyTargets;
        result.reason = "too-many-targets";
        return result;
    }

    for (std::size_t i = 0; i < inv.target_count; ++i) {
        const auto& target = inv.targets[i];

        if (target.route_id[0] == '\0') {
            result.status = InvalidationTargetResolveStatus::InvalidArgument;
            result.reason = "empty-target-route-id";
            return result;
        }

        const auto* target_route = find_route_by_id(routes, route_count, target.route_id);

        if (target_route == nullptr) {
            result.status = InvalidationTargetResolveStatus::UnknownTargetRoute;
            result.reason = "unknown-target-route";
            return result;
        }

        if (std::strcmp(target_route->route_id, mutation_route->route_id) == 0) {
            result.status = InvalidationTargetResolveStatus::SelfTarget;
            result.reason = "self-target";
            return result;
        }

        if (target_route->allowed_method != policy::HttpMethod::Get) {
            result.status = InvalidationTargetResolveStatus::TargetNotGet;
            result.reason = "target-not-get";
            return result;
        }

        if (!target_route->cache.enabled) {
            result.status = InvalidationTargetResolveStatus::TargetCacheDisabled;
            result.reason = "target-cache-disabled";
            return result;
        }

        if (target_route->cache.behavior != policy::CacheBehavior::Store) {
            result.status = InvalidationTargetResolveStatus::TargetCacheNotStore;
            result.reason = "target-cache-not-store";
            return result;
        }

        // Strategy check (currently only RouteEpoch supported)
        if (target.strategy != policy::CacheInvalidationStrategy::RouteEpoch) {
            result.status = InvalidationTargetResolveStatus::UnsupportedStrategy;
            result.reason = "unsupported-strategy";
            return result;
        }

        auto& resolved = result.targets[result.target_count++];
        std::strncpy(resolved.route_id, target.route_id, sizeof(resolved.route_id) - 1);
        resolved.strategy = InvalidationTargetStrategy::RouteEpoch;
    }

    result.success_status_min = inv.success_status_min;
    result.success_status_max = inv.success_status_max;
    result.ok = true;
    result.status = InvalidationTargetResolveStatus::Resolved;
    result.reason = "resolved";

    return result;
}

} // namespace bytetaper::cache

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CACHE_CACHE_INVALIDATION_TARGET_RESOLVER_H
#define BYTETAPER_CACHE_CACHE_INVALIDATION_TARGET_RESOLVER_H

#include "policy/route_policy.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::cache {

static constexpr std::size_t kMaxResolvedInvalidationTargets = 8;
static constexpr std::size_t kInvalidationRouteIdMaxLen = 64;

enum class InvalidationTargetStrategy : std::uint8_t { RouteEpoch = 0 };

enum class InvalidationTargetResolveStatus : std::uint8_t {
    Resolved,
    NoPolicy,
    Disabled,
    NonMutationMethod,
    MethodNotEnabled,
    NoTargets,
    TooManyTargets,
    UnknownTargetRoute,
    TargetNotGet,
    TargetCacheDisabled,
    TargetCacheNotStore,
    UnsupportedStrategy,
    SelfTarget,
    InvalidArgument,
};

struct ResolvedInvalidationTarget {
    char route_id[kInvalidationRouteIdMaxLen] = {};
    InvalidationTargetStrategy strategy = InvalidationTargetStrategy::RouteEpoch;
};

struct InvalidationTargetResolveResult {
    bool ok = false;
    InvalidationTargetResolveStatus status = InvalidationTargetResolveStatus::InvalidArgument;
    const char* reason = nullptr;
    ResolvedInvalidationTarget targets[kMaxResolvedInvalidationTargets] = {};
    std::size_t target_count = 0;
    std::uint16_t success_status_min = 200;
    std::uint16_t success_status_max = 299;
};

InvalidationTargetResolveResult
resolve_invalidation_targets(const policy::RoutePolicy* routes, std::size_t route_count,
                             const policy::RoutePolicy* mutation_route,
                             policy::HttpMethod request_method);

} // namespace bytetaper::cache

#endif // BYTETAPER_CACHE_CACHE_INVALIDATION_TARGET_RESOLVER_H

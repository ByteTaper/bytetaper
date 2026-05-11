// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_POLICY_ROUTE_MATCHER_H
#define BYTETAPER_POLICY_ROUTE_MATCHER_H

#include "hash/hash.h"
#include "metrics/runtime_metrics.h"
#include "policy/route_policy.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bytetaper::policy {

static constexpr std::size_t kMaxCompiledRoutes = 64;

enum class RouteMatcherStrategy : std::uint8_t {
    LinearSmall,
    ExactHashPrefixLinear,
    PrefixTrieExperimental,
};

static constexpr std::size_t kExactHashTableSize = 128;
static constexpr std::size_t kExactHashStrategyThreshold = 8;

struct ExactRouteHashTable {
    const char* paths[kExactHashTableSize] = {};
    const RoutePolicy* policies[kExactHashTableSize] = {};
    std::uint32_t original_orders[kExactHashTableSize] = {};
};

struct CompiledExactRoute {
    const char* path = nullptr;
    const RoutePolicy* policy = nullptr;
    std::uint32_t original_order = 0;
};

struct CompiledPrefixRoute {
    const char* prefix = nullptr;
    std::size_t prefix_len = 0;
    const RoutePolicy* policy = nullptr;
    std::uint32_t original_order = 0;
};

struct CompiledRouteMatcher {
    RouteMatcherStrategy strategy = RouteMatcherStrategy::LinearSmall;

    CompiledExactRoute exact_routes[kMaxCompiledRoutes];
    std::size_t exact_count = 0;

    CompiledPrefixRoute prefix_routes[kMaxCompiledRoutes];
    std::size_t prefix_count = 0;

    ExactRouteHashTable exact_hash_table;
};

inline CompiledRouteMatcher* compile_route_matcher(const RoutePolicy* policies, std::size_t count,
                                                   CompiledRouteMatcher* matcher) {
    matcher->strategy = RouteMatcherStrategy::LinearSmall;
    matcher->exact_count = 0;
    matcher->prefix_count = 0;

    for (std::size_t i = 0; i < count; ++i) {
        const RoutePolicy& p = policies[i];
        if (p.match_prefix == nullptr) {
            continue;
        }

        if (p.match_kind == RouteMatchKind::Exact) {
            if (matcher->exact_count < kMaxCompiledRoutes) {
                auto& e = matcher->exact_routes[matcher->exact_count++];
                e.path = p.match_prefix;
                e.policy = &p;
                e.original_order = static_cast<std::uint32_t>(i);
            }
        } else {
            if (matcher->prefix_count < kMaxCompiledRoutes) {
                auto& pr = matcher->prefix_routes[matcher->prefix_count++];
                pr.prefix = p.match_prefix;
                pr.prefix_len = std::strlen(p.match_prefix);
                pr.policy = &p;
                pr.original_order = static_cast<std::uint32_t>(i);
            }
        }
    }

    // Auto-select strategy based on exact route count
    matcher->strategy = (matcher->exact_count > kExactHashStrategyThreshold)
                            ? RouteMatcherStrategy::ExactHashPrefixLinear
                            : RouteMatcherStrategy::LinearSmall;

    // Build hash table if using hash strategy
    if (matcher->strategy == RouteMatcherStrategy::ExactHashPrefixLinear) {
        for (std::size_t i = 0; i < kExactHashTableSize; ++i) {
            matcher->exact_hash_table.paths[i] = nullptr;
            matcher->exact_hash_table.policies[i] = nullptr;
            matcher->exact_hash_table.original_orders[i] = 0;
        }

        for (std::size_t i = 0; i < matcher->exact_count; ++i) {
            const auto& e = matcher->exact_routes[i];
            std::uint32_t h = bytetaper::hash::hash_cstr_runtime(e.path);
            std::size_t slot = h & (kExactHashTableSize - 1);
            while (matcher->exact_hash_table.paths[slot] != nullptr) {
                slot = (slot + 1) & (kExactHashTableSize - 1);
            }
            matcher->exact_hash_table.paths[slot] = e.path;
            matcher->exact_hash_table.policies[slot] = e.policy;
            matcher->exact_hash_table.original_orders[slot] = e.original_order;
        }
    }

    return matcher;
}

inline CompiledRouteMatcher* compile_route_matcher(const RoutePolicy* policies, std::size_t count,
                                                   CompiledRouteMatcher* matcher,
                                                   RouteMatcherStrategy force_strategy) {
    compile_route_matcher(policies, count, matcher);
    matcher->strategy = force_strategy;
    if (matcher->strategy == RouteMatcherStrategy::ExactHashPrefixLinear) {
        for (std::size_t i = 0; i < kExactHashTableSize; ++i) {
            matcher->exact_hash_table.paths[i] = nullptr;
            matcher->exact_hash_table.policies[i] = nullptr;
            matcher->exact_hash_table.original_orders[i] = 0;
        }

        for (std::size_t i = 0; i < matcher->exact_count; ++i) {
            const auto& e = matcher->exact_routes[i];
            std::uint32_t h = bytetaper::hash::hash_cstr_runtime(e.path);
            std::size_t slot = h & (kExactHashTableSize - 1);
            while (matcher->exact_hash_table.paths[slot] != nullptr) {
                slot = (slot + 1) & (kExactHashTableSize - 1);
            }
            matcher->exact_hash_table.paths[slot] = e.path;
            matcher->exact_hash_table.policies[slot] = e.policy;
            matcher->exact_hash_table.original_orders[slot] = e.original_order;
        }
    }
    return matcher;
}

inline const RoutePolicy* match_route_compiled(const CompiledRouteMatcher& matcher,
                                               const char* request_path,
                                               metrics::RuntimeMetrics* metrics) {
    if (request_path == nullptr) {
        return nullptr;
    }

    const RoutePolicy* best_exact_policy = nullptr;
    std::uint32_t best_exact_order = UINT32_MAX;
    const CompiledPrefixRoute* best_prefix = nullptr;

    // --- Exact matching ---
    if (matcher.strategy == RouteMatcherStrategy::ExactHashPrefixLinear) {
        const std::uint32_t h = bytetaper::hash::hash_cstr_runtime(request_path);
        std::size_t slot = h & (kExactHashTableSize - 1);
        while (matcher.exact_hash_table.paths[slot] != nullptr) {
            if (std::strcmp(matcher.exact_hash_table.paths[slot], request_path) == 0) {
                metrics::record_runtime_event(metrics,
                                              metrics::RuntimeMetricEvent::RouteMatchExactHashHit);
                best_exact_policy = matcher.exact_hash_table.policies[slot];
                best_exact_order = matcher.exact_hash_table.original_orders[slot];
                break;
            }
            slot = (slot + 1) & (kExactHashTableSize - 1);
        }
    } else {
        for (std::size_t i = 0; i < matcher.exact_count; ++i) {
            metrics::record_runtime_event(metrics,
                                          metrics::RuntimeMetricEvent::RouteMatchExactScan);
            if (std::strcmp(request_path, matcher.exact_routes[i].path) == 0) {
                best_exact_policy = matcher.exact_routes[i].policy;
                best_exact_order = matcher.exact_routes[i].original_order;
                break;
            }
        }
    }

    // --- Prefix matching (always linear) ---
    for (std::size_t i = 0; i < matcher.prefix_count; ++i) {
        metrics::record_runtime_event(metrics, metrics::RuntimeMetricEvent::RouteMatchPrefixScan);
        const auto& pr = matcher.prefix_routes[i];
        if (std::strncmp(request_path, pr.prefix, pr.prefix_len) == 0) {
            best_prefix = &pr;
            break;
        }
    }

    // --- Tiebreak and no-match ---
    if (best_exact_policy == nullptr && best_prefix == nullptr) {
        metrics::record_runtime_event(metrics, metrics::RuntimeMetricEvent::RouteMatchNoMatch);
        return nullptr;
    }
    if (best_exact_policy == nullptr) {
        return best_prefix->policy;
    }
    if (best_prefix == nullptr) {
        return best_exact_policy;
    }

    // Both matched — return whichever appeared first in the original array
    return (best_exact_order < best_prefix->original_order) ? best_exact_policy
                                                            : best_prefix->policy;
}

inline const RoutePolicy* match_route_compiled(const CompiledRouteMatcher& matcher,
                                               const char* request_path) {
    return match_route_compiled(matcher, request_path, nullptr);
}

/**
 * Finds the first RoutePolicy in [policies, policies+count) whose match_prefix
 * matches `request_path` according to the policy's match_kind.
 *
 * Returns a pointer to the matching RoutePolicy (into the caller's array),
 * or nullptr if no policy matches or if request_path is null.
 *
 * First-match-wins order is guaranteed.
 */
inline const RoutePolicy* match_route_by_path(const RoutePolicy* policies, std::size_t count,
                                              const char* request_path) {
    if (policies == nullptr || request_path == nullptr) {
        return nullptr;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const RoutePolicy& policy = policies[i];

        if (policy.match_prefix == nullptr) {
            continue;
        }

        if (policy.match_kind == RouteMatchKind::Exact) {
            if (std::strcmp(request_path, policy.match_prefix) == 0) {
                return &policy;
            }
        } else {
            // Default: Prefix
            const std::size_t prefix_len = std::strlen(policy.match_prefix);
            if (std::strncmp(request_path, policy.match_prefix, prefix_len) == 0) {
                return &policy;
            }
        }
    }

    return nullptr;
}

} // namespace bytetaper::policy

#endif // BYTETAPER_POLICY_ROUTE_MATCHER_H

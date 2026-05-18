// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_cache_namespace_versioning.h"

namespace bytetaper::taperquery {

namespace {

bool is_cache_impacting(TqSemanticImpact impact) {
    switch (impact) {
    case TqSemanticImpact::FieldFilteringBehavior:
    case TqSemanticImpact::CacheBehavior:
    case TqSemanticImpact::CacheKeyBehavior:
    case TqSemanticImpact::CacheStorageBehavior:
        return true;
    default:
        return false;
    }
}

std::string semantic_impact_to_string(TqSemanticImpact impact) {
    switch (impact) {
    case TqSemanticImpact::FieldFilteringBehavior:
        return "FieldFilteringBehavior";
    case TqSemanticImpact::CacheBehavior:
        return "CacheBehavior";
    case TqSemanticImpact::CacheKeyBehavior:
        return "CacheKeyBehavior";
    case TqSemanticImpact::CacheStorageBehavior:
        return "CacheStorageBehavior";
    default:
        return "Other";
    }
}

TqCacheNamespaceVersioningResult
detect_and_optionally_bump(const TqApplyPlan& plan, runtime::RouteCacheEpochStore* epoch_store,
                           bool perform_bump) {
    TqCacheNamespaceVersioningResult result;
    result.ok = true;

    for (const auto& rc : plan.route_changes) {
        TqRouteCacheNamespaceChange change;
        change.route_id = rc.route_id;
        change.before_route_identity = rc.before_identity;
        change.after_route_identity = rc.after_identity;
        change.epoch_bump_required = false;

        std::uint64_t before_val = 0;
        if (epoch_store) {
            auto get_res =
                runtime::route_cache_epoch_get(epoch_store, rc.route_id.c_str(), &before_val);
            if (get_res == runtime::RouteCacheEpochResult::NotFound) {
                before_val = 0;
            }
        }
        change.before_epoch = before_val;

        if (rc.kind == TqRouteChangeKind::Added) {
            if (perform_bump && epoch_store) {
                auto reg_res =
                    runtime::route_cache_epoch_register(epoch_store, rc.route_id.c_str());
                if (reg_res != runtime::RouteCacheEpochResult::Ok) {
                    result.ok = false;
                    result.error = "Failed to register added route: " + rc.route_id;
                    return result;
                }
                std::uint64_t after_val = 0;
                runtime::route_cache_epoch_get(epoch_store, rc.route_id.c_str(), &after_val);
                change.after_epoch = after_val;
            } else {
                change.after_epoch = runtime::kInitialRouteCacheEpoch;
            }
            result.changed_routes.push_back(std::move(change));
        } else if (rc.kind == TqRouteChangeKind::Removed) {
            change.after_epoch = before_val;
            result.changed_routes.push_back(std::move(change));
        } else if (rc.kind == TqRouteChangeKind::Modified) {
            bool bump_req = false;
            std::vector<std::string> reasons;
            for (const auto& fc : rc.field_changes) {
                if (is_cache_impacting(fc.impact)) {
                    bump_req = true;
                    reasons.push_back(semantic_impact_to_string(fc.impact));
                }
            }

            if (bump_req) {
                change.epoch_bump_required = true;
                change.reasons = std::move(reasons);

                if (perform_bump && epoch_store) {
                    std::uint64_t after_val = 0;
                    auto bump_res = runtime::route_cache_epoch_bump(
                        epoch_store, rc.route_id.c_str(), &after_val);
                    if (bump_res != runtime::RouteCacheEpochResult::Ok) {
                        result.ok = false;
                        result.error = "Failed to bump epoch for route: " + rc.route_id;
                        return result;
                    }
                    change.after_epoch = after_val;
                } else {
                    change.after_epoch =
                        (before_val == 0 ? runtime::kInitialRouteCacheEpoch + 1 : before_val + 1);
                }
            } else {
                change.after_epoch = before_val;
            }
            result.changed_routes.push_back(std::move(change));
        }
    }

    return result;
}

} // namespace

TqCacheNamespaceVersioningResult
version_cache_namespace_for_apply_plan(const TqApplyPlan& plan,
                                       runtime::RouteCacheEpochStore* epoch_store) {
    if (epoch_store) {
        for (const auto& rc : plan.route_changes) {
            if (rc.kind == TqRouteChangeKind::Added) {
                runtime::route_cache_epoch_register(epoch_store, rc.route_id.c_str());
            }
        }
    }

    return detect_and_optionally_bump(plan, epoch_store, /*perform_bump=*/true);
}

TqCacheNamespaceVersioningResult
detect_cache_namespace_impacts(const TqApplyPlan& plan,
                               runtime::RouteCacheEpochStore* epoch_store) {
    return detect_and_optionally_bump(plan, epoch_store, /*perform_bump=*/false);
}

} // namespace bytetaper::taperquery

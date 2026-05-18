// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/tq_cache_namespace_versioning.h"

#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "metrics/cache_metrics.h"

namespace bytetaper::taperquery {

RouteCacheCleanupQueueImpl::RouteCacheCleanupQueueImpl(cache::L2DiskCache* l2_cache,
                                                       metrics::CacheMetrics* metrics)
    : l2_cache_(l2_cache), metrics_(metrics) {}

void RouteCacheCleanupQueueImpl::execute_cleanup(const RouteCacheCleanupJob& job) {
    std::fprintf(stdout,
                 "[RouteCacheCleanup] Asynchronously clearing old epoch namespace %llu for "
                 "route '%s'\n",
                 static_cast<unsigned long long>(job.old_epoch), job.route_id.c_str());
    std::fprintf(stdout,
                 "[RouteCacheCleanup] Physical L2 disk cleanup for route '%s' is partial/deferred; "
                 "relying on L2 entry TTL expiration\n",
                 job.route_id.c_str());
    std::fflush(stdout);

    if (metrics_ != nullptr) {
        metrics_->l2_remove_enqueued_total++;
        metrics_->l2_remove_failed_total++;
    }
}

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
            change.would_cleanup_l1 = true;
            change.would_cleanup_l2 = true;
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
                change.would_cleanup_l1 = true;
                change.would_cleanup_l2 = true;
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

TqCacheNamespaceApplyResult version_and_cleanup_cache_namespaces_for_apply(
    const TqApplyPlan& plan, runtime::RouteCacheEpochStore* epoch_store, cache::L1Cache* l1_cache,
    RouteCacheCleanupQueue* l2_cleanup_queue) {
    auto epoch_res = version_epochs_for_apply(plan, epoch_store);
    if (!epoch_res.ok) {
        return epoch_res;
    }
    return cleanup_cache_namespaces_for_apply(epoch_res, l1_cache, l2_cleanup_queue);
}

TqCacheNamespaceApplyResult version_epochs_for_apply(const TqApplyPlan& plan,
                                                     runtime::RouteCacheEpochStore* epoch_store) {
    TqCacheNamespaceApplyResult result;
    result.ok = true;

    for (const auto& rc : plan.route_changes) {
        TqRouteCacheCleanupPlan route_plan;
        route_plan.route_id = rc.route_id;
        route_plan.before_route_identity = rc.before_identity;
        route_plan.after_route_identity = rc.after_identity;

        std::uint64_t before_val = 0;
        if (epoch_store) {
            auto get_res =
                runtime::route_cache_epoch_get(epoch_store, rc.route_id.c_str(), &before_val);
            if (get_res == runtime::RouteCacheEpochResult::NotFound) {
                before_val = 0;
            }
        }
        route_plan.old_epoch = before_val;

        if (rc.kind == TqRouteChangeKind::Added) {
            if (epoch_store) {
                auto reg_res =
                    runtime::route_cache_epoch_register(epoch_store, rc.route_id.c_str());
                if (reg_res != runtime::RouteCacheEpochResult::Ok) {
                    result.ok = false;
                    result.error = "Failed to register added route: " + rc.route_id;
                    return result;
                }
                std::uint64_t after_val = 0;
                runtime::route_cache_epoch_get(epoch_store, rc.route_id.c_str(), &after_val);
                route_plan.new_epoch = after_val;
            } else {
                route_plan.new_epoch = runtime::kInitialRouteCacheEpoch;
            }
            result.routes.push_back(std::move(route_plan));
        } else if (rc.kind == TqRouteChangeKind::Removed) {
            route_plan.new_epoch = before_val;
            route_plan.l1_cleanup_required = true;
            route_plan.l2_cleanup_required = true;
            result.routes.push_back(std::move(route_plan));
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
                route_plan.l1_cleanup_required = true;
                route_plan.l2_cleanup_required = true;
                route_plan.reasons = std::move(reasons);

                if (epoch_store) {
                    std::uint64_t after_val = 0;
                    auto bump_res = runtime::route_cache_epoch_bump(
                        epoch_store, rc.route_id.c_str(), &after_val);
                    if (bump_res != runtime::RouteCacheEpochResult::Ok) {
                        result.ok = false;
                        result.error = "Failed to bump epoch for route: " + rc.route_id;
                        return result;
                    }
                    route_plan.new_epoch = after_val;
                } else {
                    route_plan.new_epoch =
                        (before_val == 0 ? runtime::kInitialRouteCacheEpoch + 1 : before_val + 1);
                }
            } else {
                route_plan.new_epoch = before_val;
            }
            result.routes.push_back(std::move(route_plan));
        }
    }

    return result;
}

TqCacheNamespaceApplyResult
cleanup_cache_namespaces_for_apply(const TqCacheNamespaceApplyResult& epoch_res,
                                   cache::L1Cache* l1_cache,
                                   RouteCacheCleanupQueue* l2_cleanup_queue) {
    TqCacheNamespaceApplyResult result = epoch_res;

    for (auto& route_plan : result.routes) {
        // L1 Cleanup (Synchronous)
        if (route_plan.l1_cleanup_required && l1_cache != nullptr) {
            cache::L1RouteNamespaceCleanupRequest l1_req;
            l1_req.route_id = route_plan.route_id.c_str();
            l1_req.old_epoch = route_plan.old_epoch;
            l1_req.old_policy_identity = route_plan.before_route_identity.c_str();
            l1_req.include_variant_entries = true;

            auto l1_res = cache::l1_cleanup_route_namespace(l1_cache, l1_req);
            route_plan.l1_removed_count = l1_res.removed_count;
            if (!l1_res.ok) {
                route_plan.warnings.push_back("L1 cleanup failed: " + l1_res.error);
            }
        }

        // L2 Cleanup (Asynchronous Enqueue)
        if (route_plan.l2_cleanup_required && l2_cleanup_queue != nullptr) {
            RouteCacheCleanupJob l2_job;
            l2_job.route_id = route_plan.route_id;
            l2_job.old_epoch = route_plan.old_epoch;
            l2_job.new_epoch = route_plan.new_epoch;
            l2_job.before_policy_identity = route_plan.before_route_identity;
            l2_job.after_policy_identity = route_plan.after_route_identity;

            l2_cleanup_queue->enqueue(l2_job);
            route_plan.l2_cleanup_enqueued = true;
        }
    }

    return result;
}

} // namespace bytetaper::taperquery

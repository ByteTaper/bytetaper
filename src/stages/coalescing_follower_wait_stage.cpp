// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/coalescing_follower_wait_stage.h"

#include "coalescing/coalescing_timeout.h"
#include "coalescing/follower_wait_pool.h"
#include "coalescing/inflight_registry.h"
#include "metrics/coalescing_metrics.h"
#include "stages/l1_cache_lookup_stage.h"

#include <chrono>
#include <cstring>

namespace bytetaper::stages {

apg::StageOutput coalescing_follower_wait_stage(apg::ApgTransformContext& context) {
    if (context.coalescing_decision.action != coalescing::CoalescingAction::Follower) {
        return { apg::StageResult::Continue };
    }

    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue };
    }

    const auto& policy = context.matched_policy->coalescing;

    // Cache Integration Check
    if (policy.require_cache_enabled &&
        context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {

        // Follower bypasses waiting, and deregisters itself.
        if (context.coalescing_registry != nullptr) {
            coalescing::registry_remove_waiter(context.coalescing_registry,
                                               context.coalescing_decision.key);
        }
        context.coalescing_decision.action = coalescing::CoalescingAction::Bypass;
        record_coalescing_event(context.coalescing_metrics, metrics::CoalescingMetricEvent::Bypass);
        return { apg::StageResult::Continue, "cache-disabled-bypassed" };
    }

    const std::uint32_t follower_wait_budget_ms =
        policy.backend_timeout_ms + policy.handoff_buffer_ms;
    if (context.coalescing_registry == nullptr) {
        context.coalescing_decision.action = coalescing::CoalescingAction::Bypass;
        record_coalescing_event(context.coalescing_metrics, metrics::CoalescingMetricEvent::Bypass);
        return { apg::StageResult::Continue, "no-registry" };
    }

    // Step 1: L1 Check before waiting
    apg::StageOutput l1_res = l1_cache_lookup_stage(context);
    if (l1_res.result == apg::StageResult::SkipRemaining) {
        if (context.coalescing_registry != nullptr) {
            coalescing::registry_remove_waiter(context.coalescing_registry,
                                               context.coalescing_decision.key);
        }
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerL1Hit);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerCacheHit);
        return l1_res;
    }

    // Step 2: Block until leader completes or timeout.
    // Try to delegate wait to dedicated pool when available, otherwise direct wait.
    coalescing::RegistryWaitResult wait_result = coalescing::RegistryWaitResult::Missing;
    coalescing::RegistrySharedResponseOutput shared{};
    bool submitted = false;

    if (context.follower_wait_pool != nullptr) {
        submitted = coalescing::follower_wait_pool_submit_and_wait(
            context.follower_wait_pool, context.coalescing_decision.key, follower_wait_budget_ms,
            context.coalescing_decision.lifecycle_generation, &shared, &wait_result);
        if (!submitted) {
            coalescing::handle_timeout_fallback(context.coalescing_registry,
                                                context.coalescing_decision);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerPoolQueueFull);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::Fallback);
            return { apg::StageResult::Continue, "pool-queue-full-fallback" };
        }
    } else {
        wait_result = coalescing::registry_wait_for_completion(
            context.coalescing_registry, context.coalescing_decision.key, follower_wait_budget_ms,
            context.coalescing_decision.lifecycle_generation, &shared);
    }

    // Step 3: Handle fast-path delivery if snapshot is ready
    if (wait_result == coalescing::RegistryWaitResult::SharedResponseReady) {
        context.cache_hit = true;
        context.cache_layer = "COALESCED";
        context.should_return_immediate_response = true;
        context.cached_response.status_code = shared.status_code;
        std::strncpy(context.cached_response.content_type, shared.content_type,
                     cache::kCacheContentTypeMaxLen - 1);
        context.cached_response.content_type[cache::kCacheContentTypeMaxLen - 1] = '\0';
        std::memcpy(context.l2_body_buf, shared.body, shared.body_len);
        context.cached_response.body = context.l2_body_buf;
        context.cached_response.body_len = shared.body_len;

        coalescing::registry_remove_waiter(context.coalescing_registry,
                                           context.coalescing_decision.key);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerSharedResponse);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerCacheHit);
        return { apg::StageResult::SkipRemaining, "coalesced-shared-response" };
    } else if (wait_result == coalescing::RegistryWaitResult::L1Ready) {
        apg::StageOutput l1 = l1_cache_lookup_stage(context);
        if (l1.result == apg::StageResult::SkipRemaining) {
            if (context.coalescing_registry != nullptr) {
                coalescing::registry_remove_waiter(context.coalescing_registry,
                                                   context.coalescing_decision.key);
            }
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerL1Ready);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerL1Hit);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerCacheHit);
            return l1;
        }
        // L1Ready wakeup but L1 lookup missed — unexpected, record and fallback.
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerL1Ready);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerL1ReadyButMiss);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::Fallback);
        coalescing::handle_timeout_fallback(context.coalescing_registry,
                                            context.coalescing_decision);
        return { apg::StageResult::Continue, "l1-ready-but-miss-fallback" };
    }

    // Step 4: L1 check after wake (whether Completed/Stored but no snapshot, Timeout, or Missing)
    l1_res = l1_cache_lookup_stage(context);
    if (l1_res.result == apg::StageResult::SkipRemaining) {
        if (context.coalescing_registry != nullptr) {
            coalescing::registry_remove_waiter(context.coalescing_registry,
                                               context.coalescing_decision.key);
        }
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerL1Hit);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerCacheHit);
        return l1_res;
    }

    // Step 5: Map wait result to granular outcomes and fallback
    if (wait_result == coalescing::RegistryWaitResult::Timeout) {
        // Check if leader published just as timeout fired (race-edge case)
        coalescing::RegistrySharedResponseOutput late_check{};
        auto recheck = coalescing::registry_wait_for_completion(
            context.coalescing_registry, context.coalescing_decision.key,
            0, // immediate poll
            context.coalescing_decision.lifecycle_generation, &late_check);

        if (recheck == coalescing::RegistryWaitResult::SharedResponseReady) {
            // Leader published just after timeout — consume result
            context.cache_hit = true;
            context.cache_layer = "COALESCED";
            context.should_return_immediate_response = true;
            context.cached_response.status_code = late_check.status_code;
            std::strncpy(context.cached_response.content_type, late_check.content_type,
                         cache::kCacheContentTypeMaxLen - 1);
            context.cached_response.content_type[cache::kCacheContentTypeMaxLen - 1] = '\0';
            std::memcpy(context.l2_body_buf, late_check.body, late_check.body_len);
            context.cached_response.body = context.l2_body_buf;
            context.cached_response.body_len = late_check.body_len;

            coalescing::registry_remove_waiter(context.coalescing_registry,
                                               context.coalescing_decision.key);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerTimeoutAfterPublish);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerCacheHit);
            return { apg::StageResult::SkipRemaining, "coalesced-shared-response-late" };
        } else {
            // Genuine timeout — leader not yet done
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerTimeoutBeforePublish);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerTimeout);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::Fallback);
            coalescing::handle_timeout_fallback(context.coalescing_registry,
                                                context.coalescing_decision);
            return { apg::StageResult::Continue, "timeout-fallback" };
        }
    } else if (wait_result == coalescing::RegistryWaitResult::Missing) {
        // Entry expired/recycled. Try L1 — leader may have stored before entry was gone.
        apg::StageOutput l1 = l1_cache_lookup_stage(context);
        if (l1.result == apg::StageResult::SkipRemaining) {
            if (context.coalescing_registry != nullptr) {
                coalescing::registry_remove_waiter(context.coalescing_registry,
                                                   context.coalescing_decision.key);
            }
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerL1Hit);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerCacheHit);
            return l1;
        }
        // Truly missing — no L1 entry either.
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerMissing);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::Fallback);
        coalescing::handle_timeout_fallback(context.coalescing_registry,
                                            context.coalescing_decision);
        return { apg::StageResult::Continue, "missing-fallback" };
    } else if (wait_result == coalescing::RegistryWaitResult::NotCacheable) {
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerNotCacheable);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::Fallback);
        coalescing::handle_timeout_fallback(context.coalescing_registry,
                                            context.coalescing_decision);
        return { apg::StageResult::Continue, "not-cacheable-fallback" };
    } else if (wait_result == coalescing::RegistryWaitResult::Failed) {
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerFailed);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::Fallback);
        coalescing::handle_timeout_fallback(context.coalescing_registry,
                                            context.coalescing_decision);
        return { apg::StageResult::Continue, "leader-failed-fallback" };
    } else if (wait_result == coalescing::RegistryWaitResult::StoredButNoSnapshot) {
        // Try L1 — leader may have stored to cache without shared snapshot
        apg::StageOutput l1 = l1_cache_lookup_stage(context);
        if (l1.result == apg::StageResult::SkipRemaining) {
            if (context.coalescing_registry != nullptr) {
                coalescing::registry_remove_waiter(context.coalescing_registry,
                                                   context.coalescing_decision.key);
            }
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerL1Hit);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerCacheHit);
            return l1;
        }
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerStoredButNoSnapshot);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::Fallback);
        coalescing::handle_timeout_fallback(context.coalescing_registry,
                                            context.coalescing_decision);
        return { apg::StageResult::Continue, "stored-no-snapshot-fallback" };
    }

    // This should never be reached with the above explicit handling.
    // Record FollowerUnaccounted to surface any remaining gaps.
    record_coalescing_event(context.coalescing_metrics,
                            metrics::CoalescingMetricEvent::FollowerUnaccounted);
    record_coalescing_event(context.coalescing_metrics, metrics::CoalescingMetricEvent::Fallback);
    coalescing::handle_timeout_fallback(context.coalescing_registry, context.coalescing_decision);
    return { apg::StageResult::Continue, "unaccounted-fallback" };
}

} // namespace bytetaper::stages

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

    const std::uint32_t wait_window_ms = policy.wait_window_ms;
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
            context.follower_wait_pool, context.coalescing_decision.key, wait_window_ms, &shared,
            &wait_result);
        if (!submitted) {
            coalescing::handle_timeout_fallback(context.coalescing_registry,
                                                context.coalescing_decision);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::Fallback);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerTimeout);
            record_upstream_call_reason(context.coalescing_metrics,
                                        metrics::UpstreamCallReason::FollowerTimeoutFallback);
            return { apg::StageResult::Continue, "pool-queue-full-fallback" };
        }
    } else {
        wait_result = coalescing::registry_wait_for_completion(
            context.coalescing_registry, context.coalescing_decision.key, wait_window_ms, &shared);
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
                                metrics::CoalescingMetricEvent::FollowerCacheHit);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerServedFromResult);
        return { apg::StageResult::SkipRemaining, "coalesced-shared-response" };
    }

    // Step 4: L1 check after wait (whether Completed/Stored but no snapshot, Timeout, or Missing)
    l1_res = l1_cache_lookup_stage(context);
    if (l1_res.result == apg::StageResult::SkipRemaining) {
        if (context.coalescing_registry != nullptr) {
            coalescing::registry_remove_waiter(context.coalescing_registry,
                                               context.coalescing_decision.key);
        }
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerCacheHit);
        return l1_res;
    }

    // L1 miss after wait — fallback upstream
    if (wait_result == coalescing::RegistryWaitResult::Timeout ||
        wait_result == coalescing::RegistryWaitResult::Missing ||
        wait_result == coalescing::RegistryWaitResult::NotCacheable ||
        wait_result == coalescing::RegistryWaitResult::Failed) {
        coalescing::handle_timeout_fallback(context.coalescing_registry,
                                            context.coalescing_decision);
        if (wait_result == coalescing::RegistryWaitResult::Timeout) {
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerTimeout);
        } else {
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerFallback);
        }
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::Fallback);
        record_upstream_call_reason(context.coalescing_metrics,
                                    metrics::UpstreamCallReason::FollowerTimeoutFallback);
        return { apg::StageResult::Continue, "timeout-fallback" };
    }

    // Leader completed but L1 still miss — leader may not have cached, was evicted, or
    // non-cacheable/failed
    coalescing::handle_timeout_fallback(context.coalescing_registry, context.coalescing_decision);
    record_coalescing_event(context.coalescing_metrics,
                            metrics::CoalescingMetricEvent::FollowerFallback);
    record_coalescing_event(context.coalescing_metrics, metrics::CoalescingMetricEvent::Fallback);
    record_upstream_call_reason(context.coalescing_metrics,
                                metrics::UpstreamCallReason::FollowerTimeoutFallback);
    return { apg::StageResult::Continue, "leader-complete-l1-miss" };
}

} // namespace bytetaper::stages

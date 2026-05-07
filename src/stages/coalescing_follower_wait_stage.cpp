// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/coalescing_follower_wait_stage.h"

#include "coalescing/coalescing_timeout.h"
#include "coalescing/follower_wait_pool.h"
#include "coalescing/inflight_registry.h"
#include "metrics/coalescing_metrics.h"
#include "observability/trace.h"
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

    // Guard/end the cache lookup span early so it does not overlap with the wait span
    if (context.trace != nullptr) {
        for (std::size_t i = 0; i < context.trace->span_count; ++i) {
            auto& s = context.trace->spans[i];
            if (std::strcmp(s.name, "bytetaper.cache.l1.lookup") == 0 && s.end_unix_nano == 0) {
                auto now = std::chrono::system_clock::now();
                auto duration = now.time_since_epoch();
                std::uint64_t now_nano =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
                s.end_unix_nano = now_nano;
                if (s.end_unix_nano >= s.start_unix_nano) {
                    s.duration_nano = s.end_unix_nano - s.start_unix_nano;
                }
            }
        }
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

    // Follower attach span
    observability::TraceSpanScope attach_span{};
    if (context.trace != nullptr) {
        attach_span = observability::trace_start_span(
            context.trace, observability::kSpanCoalescingFollowerAttach,
            observability::TraceLatencyClass::ActiveProcessingDetail, &context.trace->root_span_id);
    }
    std::strncpy(context.coalescing_attach_result_str, "success",
                 sizeof(context.coalescing_attach_result_str) - 1);
    attach_span.end();

    // Step 2: Block until leader completes or timeout.
    // Try to delegate wait to dedicated pool when available, otherwise direct wait.
    coalescing::RegistryWaitResult wait_result = coalescing::RegistryWaitResult::Missing;
    coalescing::RegistrySharedResponseOutput shared{};
    bool submitted = false;

    observability::TraceSpanScope wait_span{};
    if (context.trace != nullptr) {
        wait_span = observability::trace_start_span(
            context.trace, observability::kSpanCoalescingFollowerWait,
            observability::TraceLatencyClass::RuntimeQueueWait, &context.trace->root_span_id);
    }

    if (context.coalescing_terminal_result_join_flag) {
        // Fast path: retrieve immediately without wait pool submission or waiting!
        wait_result = coalescing::registry_wait_for_completion(
            context.coalescing_registry, context.coalescing_decision.key, 0, &shared);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::TerminalResultServed);
    } else {
        record_coalescing_event(context.coalescing_metrics, metrics::CoalescingMetricEvent::Wait);
        if (context.follower_wait_pool != nullptr) {
            submitted = coalescing::follower_wait_pool_submit_and_wait(
                context.follower_wait_pool, context.coalescing_decision.key, wait_window_ms,
                &shared, &wait_result);
            if (!submitted) {
                wait_span.end();
                coalescing::handle_timeout_fallback(context.coalescing_registry,
                                                    context.coalescing_decision);
                record_coalescing_event(context.coalescing_metrics,
                                        metrics::CoalescingMetricEvent::Fallback);
                record_coalescing_event(context.coalescing_metrics,
                                        metrics::CoalescingMetricEvent::FollowerTimeout);
                record_coalescing_event(context.coalescing_metrics,
                                        metrics::CoalescingMetricEvent::Timeout);
                context.coalescing_upstream_reason =
                    metrics::UpstreamCallReason::FollowerTimeoutFallback;
                std::strncpy(context.coalescing_wakeup_reason_str, "queue_full",
                             sizeof(context.coalescing_wakeup_reason_str) - 1);
                std::strncpy(context.coalescing_decision_str, "follower_timeout_fallback",
                             sizeof(context.coalescing_decision_str) - 1);
                std::strncpy(context.coalescing_upstream_call_reason, "follower_timeout_fallback",
                             sizeof(context.coalescing_upstream_call_reason) - 1);
                return { apg::StageResult::Continue, "pool-queue-full-fallback" };
            }
        } else {
            wait_result = coalescing::registry_wait_for_completion(context.coalescing_registry,
                                                                   context.coalescing_decision.key,
                                                                   wait_window_ms, &shared);
        }
    }

    wait_span.end();

    // Step 3: Handle fast-path delivery if snapshot is ready
    if (wait_result == coalescing::RegistryWaitResult::SharedResponseReady) {
        observability::TraceSpanScope consume_span{};
        if (context.trace != nullptr) {
            consume_span = observability::trace_start_span(
                context.trace, observability::kSpanCoalescingFollowerConsumeResult,
                observability::TraceLatencyClass::ActiveProcessingDetail,
                &context.trace->root_span_id);
        }

        if (!context.coalescing_terminal_result_join_flag) {
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::Wakeup);
        }
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
        context.coalescing_result_source = "COALESCED";

        std::strncpy(context.coalescing_wakeup_reason_str, "result_ready",
                     sizeof(context.coalescing_wakeup_reason_str) - 1);
        std::strncpy(context.coalescing_decision_str, "follower_consume_result",
                     sizeof(context.coalescing_decision_str) - 1);

        coalescing::registry_remove_waiter(context.coalescing_registry,
                                           context.coalescing_decision.key);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerCacheHit);
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::FollowerServedFromResult);
        consume_span.end();
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
        context.coalescing_result_source = "L1";
        std::strncpy(context.coalescing_wakeup_reason_str, "result_ready",
                     sizeof(context.coalescing_wakeup_reason_str) - 1);
        std::strncpy(context.coalescing_decision_str, "follower_consume_result",
                     sizeof(context.coalescing_decision_str) - 1);
        return l1_res;
    }

    // L1 miss after wait — fallback upstream
    if (wait_result == coalescing::RegistryWaitResult::Timeout ||
        wait_result == coalescing::RegistryWaitResult::Missing ||
        wait_result == coalescing::RegistryWaitResult::NotCacheable ||
        wait_result == coalescing::RegistryWaitResult::Failed) {

        observability::TraceSpanScope fallback_span{};
        if (context.trace != nullptr) {
            fallback_span = observability::trace_start_span(
                context.trace, observability::kSpanCoalescingFollowerTimeout,
                observability::TraceLatencyClass::RuntimeQueueWait, &context.trace->root_span_id);
        }

        coalescing::handle_timeout_fallback(context.coalescing_registry,
                                            context.coalescing_decision);
        if (wait_result == coalescing::RegistryWaitResult::Timeout) {
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerTimeout);
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::Timeout);
            std::strncpy(context.coalescing_wakeup_reason_str, "timeout",
                         sizeof(context.coalescing_wakeup_reason_str) - 1);
        } else if (wait_result == coalescing::RegistryWaitResult::Failed) {
            std::strncpy(context.coalescing_wakeup_reason_str, "leader_failed",
                         sizeof(context.coalescing_wakeup_reason_str) - 1);
        } else if (wait_result == coalescing::RegistryWaitResult::NotCacheable) {
            std::strncpy(context.coalescing_wakeup_reason_str, "not_cacheable",
                         sizeof(context.coalescing_wakeup_reason_str) - 1);
        } else {
            std::strncpy(context.coalescing_wakeup_reason_str, "cancelled",
                         sizeof(context.coalescing_wakeup_reason_str) - 1);
        }

        std::strncpy(context.coalescing_decision_str, "follower_timeout_fallback",
                     sizeof(context.coalescing_decision_str) - 1);
        std::strncpy(context.coalescing_upstream_call_reason, "follower_timeout_fallback",
                     sizeof(context.coalescing_upstream_call_reason) - 1);

        if (wait_result != coalescing::RegistryWaitResult::Timeout) {
            record_coalescing_event(context.coalescing_metrics,
                                    metrics::CoalescingMetricEvent::FollowerFallback);
        }
        record_coalescing_event(context.coalescing_metrics,
                                metrics::CoalescingMetricEvent::Fallback);
        context.coalescing_upstream_reason = metrics::UpstreamCallReason::FollowerTimeoutFallback;
        fallback_span.end();
        return { apg::StageResult::Continue, "timeout-fallback" };
    }

    // Leader completed but L1 still miss — leader may not have cached, was evicted, or
    // non-cacheable/failed
    observability::TraceSpanScope fallback_span{};
    if (context.trace != nullptr) {
        fallback_span = observability::trace_start_span(
            context.trace, observability::kSpanCoalescingFollowerTimeout,
            observability::TraceLatencyClass::RuntimeQueueWait, &context.trace->root_span_id);
    }

    coalescing::handle_timeout_fallback(context.coalescing_registry, context.coalescing_decision);
    record_coalescing_event(context.coalescing_metrics,
                            metrics::CoalescingMetricEvent::FollowerFallback);
    record_coalescing_event(context.coalescing_metrics, metrics::CoalescingMetricEvent::Fallback);
    context.coalescing_upstream_reason = metrics::UpstreamCallReason::FollowerTimeoutFallback;
    std::strncpy(context.coalescing_wakeup_reason_str, "leader_failed",
                 sizeof(context.coalescing_wakeup_reason_str) - 1);
    std::strncpy(context.coalescing_decision_str, "follower_timeout_fallback",
                 sizeof(context.coalescing_decision_str) - 1);
    std::strncpy(context.coalescing_upstream_call_reason, "follower_timeout_fallback",
                 sizeof(context.coalescing_upstream_call_reason) - 1);
    fallback_span.end();
    return { apg::StageResult::Continue, "leader-complete-l1-miss" };
}

} // namespace bytetaper::stages

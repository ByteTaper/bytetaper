// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/l2_cache_async_store_enqueue_stage.h"

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "metrics/runtime_metrics.h"
#include "runtime/worker_queue.h"

#include <chrono>
#include <cstring>

namespace bytetaper::stages {

apg::StageOutput l2_cache_async_store_enqueue_stage(apg::ApgTransformContext& context) {
    // 1. Policy presence
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    // 2. Cache enabled
    if (context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
        return { apg::StageResult::Continue, "cache-disabled" };
    }

    // 3. GET method only
    if (context.request_method != policy::HttpMethod::Get) {
        return { apg::StageResult::Continue, "non-get" };
    }

    // 4. 2xx status only
    if (context.response_status_code < 200 || context.response_status_code >= 300) {
        return { apg::StageResult::Continue, "non-2xx" };
    }

    // 5. Body presence
    if (context.response_body == nullptr || context.response_body_len == 0) {
        return { apg::StageResult::Continue, "no-body" };
    }

    // 6. Non-zero TTL
    if (context.matched_policy->cache.ttl_seconds == 0) {
        return { apg::StageResult::Continue, "no-ttl" };
    }

    // 7. L2 and Worker pointers present
    if (context.l2_cache == nullptr) {
        return { apg::StageResult::Continue, "no-l2-cache" };
    }
    if (context.worker_queue == nullptr) {
        return { apg::StageResult::Continue, "no-worker-queue" };
    }

    // 8. Body size cap follows the worker pool configured from route max_response_bytes.
    if (context.response_body_len > context.worker_queue->async_store_max_body_size) {
        metrics::record_runtime_event(context.runtime_metrics,
                                      metrics::RuntimeMetricEvent::L2StoreOversizedSkipped);
        return { apg::StageResult::Continue, "body-too-large" };
    }

    const char* key = context.cache_key;
    std::int64_t ttl_ms =
        static_cast<std::int64_t>(context.matched_policy->cache.ttl_seconds) * 1000;

    if (context.selected_field_count > 0 && context.matched_policy->cache.field_variant.enabled) {
        if (context.variant_cache_key_ready && context.variant_admission_passed) {
            key = context.variant_cache_key;
            const auto& fv_policy = context.matched_policy->cache.field_variant;
            if (fv_policy.ttl_max_ms > 0 && ttl_ms > fv_policy.ttl_max_ms) {
                ttl_ms = fv_policy.ttl_max_ms;
            }
        } else {
            return { apg::StageResult::Continue, "has-query-selection-not-admitted-skip" };
        }
    } else if (!context.cache_key_ready) {
        return { apg::StageResult::Continue, "key-not-ready" };
    }

    runtime::L2StoreJob job{};
    std::strncpy(job.key, key, cache::kCacheKeyMaxLen - 1);
    job.key[cache::kCacheKeyMaxLen - 1] = '\0';

    // 10. Populate CacheEntry metadata
    std::memcpy(job.entry.key, job.key, sizeof(job.key));
    job.entry.status_code = context.response_status_code;
    std::memcpy(job.entry.content_type, context.response_content_type,
                sizeof(job.entry.content_type));
    job.entry.body = context.response_body; // Original body source
    job.entry.body_len = context.response_body_len;
    job.entry.original_body_len = context.input_payload_bytes;
    job.entry.removed_fields = static_cast<std::uint16_t>(context.removed_field_count);
    job.entry.created_at_epoch_ms = context.request_epoch_ms;
    job.entry.expires_at_epoch_ms =
        (context.request_epoch_ms > 0) ? context.request_epoch_ms + ttl_ms : 0;

    job.body_len = context.response_body_len;

    // Coalescing L2 handoff — populated only for leader large-body responses within buffer limits
    if (context.coalescing_decision.action == coalescing::CoalescingAction::Leader &&
        context.coalescing_registry != nullptr &&
        context.response_body_len > cache::kL1MaxBodySize &&
        context.response_body_len <= apg::ApgTransformContext::kL2BodyBufSize) {
        job.coalescing_handoff_enabled = true;
        job.coalescing_registry = context.coalescing_registry;
        std::strncpy(job.coalescing_key, context.coalescing_decision.key,
                     sizeof(job.coalescing_key) - 1);
        job.coalescing_key[sizeof(job.coalescing_key) - 1] = '\0';
        job.lifecycle_generation = context.coalescing_decision.lifecycle_generation;
    }

    // 11. Enqueue
    if (!runtime::worker_queue_try_enqueue_store(context.worker_queue, job)) {
        if (job.coalescing_handoff_enabled) {
            auto now_ms =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count());
            coalescing::registry_complete_state_if_generation(
                context.coalescing_registry, context.coalescing_decision.key,
                context.coalescing_decision.lifecycle_generation,
                coalescing::InFlightCompletionState::Failed, now_ms);
        }
        metrics::record_runtime_event(context.runtime_metrics,
                                      metrics::RuntimeMetricEvent::L2StoreDropped);
        return { apg::StageResult::Continue, "queue-full" };
    }

    metrics::record_runtime_event(context.runtime_metrics,
                                  metrics::RuntimeMetricEvent::L2StoreEnqueued);
    return { apg::StageResult::Continue, "enqueued" };
}

} // namespace bytetaper::stages

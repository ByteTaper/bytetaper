// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/l2_cache_async_lookup_enqueue_stage.h"

#include "cache/cache_key.h"
#include "runtime/worker_queue.h"

#include <cstring>

namespace bytetaper::stages {

apg::StageOutput l2_cache_async_lookup_enqueue_stage(apg::ApgTransformContext& context) {
    // 1. Skip if L1 already hit
    if (context.cache_hit) {
        return apg::StageOutput{ apg::StageResult::Continue, "l1-hit-skip" };
    }

    // 2. Pre-flight checks
    if (context.matched_policy == nullptr) {
        return apg::StageOutput{ apg::StageResult::Continue, "no-policy" };
    }
    if (context.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
        return apg::StageOutput{ apg::StageResult::Continue, "cache-disabled" };
    }
    if (context.l2_cache == nullptr) {
        return apg::StageOutput{ apg::StageResult::Continue, "no-l2-cache" };
    }
    if (context.worker_queue == nullptr) {
        return apg::StageOutput{ apg::StageResult::Continue, "no-worker-queue" };
    }

    const char* key = context.cache_key;
    if (context.selected_field_count > 0 && context.matched_policy->cache.field_variant.enabled) {
        if (context.variant_cache_key_ready && context.variant_admission_passed) {
            key = context.variant_cache_key;
        } else {
            return apg::StageOutput{ apg::StageResult::Continue,
                                     "has-query-selection-not-admitted-skip" };
        }
    } else if (!context.cache_key_ready) {
        return apg::StageOutput{ apg::StageResult::Continue, "key-not-ready" };
    }

    // 4. Enqueue
    runtime::L2LookupJob job{};
    std::strncpy(job.key, key, cache::kCacheKeyMaxLen - 1);
    job.key[cache::kCacheKeyMaxLen - 1] = '\0';

    if (!runtime::worker_queue_try_enqueue_lookup(context.worker_queue, job)) {
        // Enqueue might fail due to queue full OR already pending (internal to
        // worker_queue_try_enqueue_lookup). The worker_queue_try_enqueue_lookup function handles
        // the metrics and the internal pending state.
        return apg::StageOutput{ apg::StageResult::Continue, "enqueue-failed" };
    }

    metrics::record_runtime_event(context.runtime_metrics,
                                  metrics::RuntimeMetricEvent::L2LookupEnqueued);
    return apg::StageOutput{ apg::StageResult::Continue, "enqueued" };
}

} // namespace bytetaper::stages

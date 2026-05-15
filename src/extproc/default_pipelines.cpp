// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/default_pipelines.h"

#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_decision_stage.h"
#include "stages/coalescing_follower_wait_stage.h"
#include "stages/coalescing_leader_completion_stage.h"
#include "stages/compression_decision_stage.h"
#include "stages/field_variant_admission_stage.h"
#include "stages/l1_cache_lookup_stage.h"
#include "stages/l1_cache_store_stage.h"
#include "stages/l1_variant_lookup_stage.h"
#include "stages/l1_variant_store_stage.h"
#include "stages/l2_cache_async_lookup_enqueue_stage.h"
#include "stages/l2_cache_async_store_enqueue_stage.h"
#include "stages/mutation_invalidation_apply_stage.h"
#include "stages/mutation_invalidation_prepare_stage.h"
#include "stages/pagination_request_mutation_stage.h"

#include <iterator>

namespace bytetaper::extproc {

/**
 * HOT-PATH: request-header pipeline.
 *
 * L1 lookup runs first: a cache hit returns SkipRemaining, bypassing
 * coalescing registration, async L2 enqueue, and pagination mutation entirely.
 * Coalescing and L2 enqueue only run on L1 miss.
 *
 * See docs/runtime/RUNTIME_BOUNDARIES.md for the enforcement contract.
 */
const apg::ApgStage kLookupStages[] = {
    stages::mutation_invalidation_prepare_stage,
    stages::cache_key_prepare_stage,
    stages::field_variant_admission_stage,
    stages::l1_cache_lookup_stage,
    stages::l1_variant_lookup_stage,
    stages::coalescing_decision_stage,
    stages::coalescing_follower_wait_stage,
    stages::l2_cache_async_lookup_enqueue_stage,
    stages::pagination_request_mutation_stage,
};
const std::size_t kLookupStageCount = std::size(kLookupStages);

const apg::ApgStage kResponseStages[] = {
    stages::mutation_invalidation_apply_stage,
    stages::compression_decision_stage,
};
const std::size_t kResponseStageCount = std::size(kResponseStages);

/**
 * HOT-PATH: response-body pipeline.
 *
 * This pipeline runs synchronously on the Envoy ExtProc response thread.
 * L1 store is synchronous and fast. L2 store is async-enqueued.
 * Coalescing leader completion runs after L1 store.
 *
 * PRECONDITION: cache_key_prepare_stage must have been executed in the
 * request-header (lookup) pipeline. This stage reuses the key from context.
 *
 * See docs/runtime/RUNTIME_BOUNDARIES.md for the enforcement contract.
 */
const apg::ApgStage kStoreStages[] = {
    stages::l1_cache_store_stage,
    stages::l1_variant_store_stage,
    stages::l2_cache_async_store_enqueue_stage,
    stages::coalescing_leader_completion_stage,
};
const std::size_t kStoreStageCount = std::size(kStoreStages);

void compile_route_runtime(const policy::RoutePolicy& policy, CompiledRouteRuntime* runtime) {
    if (runtime == nullptr) {
        return;
    }

    *runtime = CompiledRouteRuntime{};
    runtime->policy = &policy;

    // 1. Lookup stages (preserving default relative ordering)
    if (policy.cache.invalidation.enabled) {
        runtime->lookup_stages[runtime->lookup_count++] =
            stages::mutation_invalidation_prepare_stage;
    }
    if (policy.cache.behavior == policy::CacheBehavior::Store) {
        runtime->lookup_stages[runtime->lookup_count++] = stages::cache_key_prepare_stage;
        if (policy.cache.field_variant.enabled) {
            runtime->lookup_stages[runtime->lookup_count++] = stages::field_variant_admission_stage;
        }
        runtime->lookup_stages[runtime->lookup_count++] = stages::l1_cache_lookup_stage;
        if (policy.cache.field_variant.enabled) {
            runtime->lookup_stages[runtime->lookup_count++] = stages::l1_variant_lookup_stage;
        }
    }
    if (policy.coalescing.enabled) {
        runtime->lookup_stages[runtime->lookup_count++] = stages::coalescing_decision_stage;
        runtime->lookup_stages[runtime->lookup_count++] = stages::coalescing_follower_wait_stage;
    }
    if (policy.cache.behavior == policy::CacheBehavior::Store) {
        runtime->lookup_stages[runtime->lookup_count++] =
            stages::l2_cache_async_lookup_enqueue_stage;
    }
    if (policy.pagination.enabled) {
        runtime->lookup_stages[runtime->lookup_count++] = stages::pagination_request_mutation_stage;
    }

    // 2. Store stages (preserving default relative ordering)
    if (policy.cache.behavior == policy::CacheBehavior::Store) {
        runtime->store_stages[runtime->store_count++] = stages::l1_cache_store_stage;
        if (policy.cache.field_variant.enabled) {
            runtime->store_stages[runtime->store_count++] = stages::l1_variant_store_stage;
        }
        runtime->store_stages[runtime->store_count++] = stages::l2_cache_async_store_enqueue_stage;
    }
    if (policy.coalescing.enabled) {
        runtime->store_stages[runtime->store_count++] = stages::coalescing_leader_completion_stage;
    }

    // 3. Response stages
    if (policy.cache.invalidation.enabled) {
        runtime->response_stages[runtime->response_count++] =
            stages::mutation_invalidation_apply_stage;
    }
    runtime->response_stages[runtime->response_count++] = stages::compression_decision_stage;
}

void compile_route_runtime_table(const policy::RoutePolicy* policies, std::size_t policy_count,
                                 CompiledRouteRuntimeTable* table) {
    if (table == nullptr) {
        return;
    }

    table->count = 0;
    if (policies == nullptr || policy_count == 0) {
        return;
    }

    std::size_t limit = policy_count;
    if (limit > kMaxCompiledRoutesCapacity) {
        limit = kMaxCompiledRoutesCapacity;
    }

    for (std::size_t i = 0; i < limit; ++i) {
        compile_route_runtime(policies[i], &table->entries[i]);
    }
    table->count = limit;
}

const CompiledRouteRuntime* find_compiled_route_runtime(const CompiledRouteRuntimeTable& table,
                                                        const policy::RoutePolicy* policy) {
    if (policy == nullptr) {
        return nullptr;
    }

    for (std::size_t i = 0; i < table.count; ++i) {
        if (table.entries[i].policy == policy) {
            return &table.entries[i];
        }
    }
    return nullptr;
}

} // namespace bytetaper::extproc

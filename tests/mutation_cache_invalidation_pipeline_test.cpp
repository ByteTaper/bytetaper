// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/default_pipelines.h"
#include "policy/route_policy.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_decision_stage.h"
#include "stages/coalescing_follower_wait_stage.h"
#include "stages/coalescing_leader_completion_stage.h"
#include "stages/compression_decision_stage.h"
#include "stages/l1_cache_lookup_stage.h"
#include "stages/l1_cache_store_stage.h"
#include "stages/mutation_invalidation_apply_stage.h"
#include "stages/mutation_invalidation_prepare_stage.h"
#include "stages/pagination_request_mutation_stage.h"

#include <gtest/gtest.h>

using namespace bytetaper::extproc;
using namespace bytetaper::stages;
using namespace bytetaper;

namespace {

TEST(MutationCacheInvalidationPipelineTest, MutationPrepareStageInsertedForInvalidationRoute) {
    policy::RoutePolicy rt_policy{};
    rt_policy.cache.invalidation.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(rt_policy, &runtime);

    bool has_prepare = false;
    for (std::size_t i = 0; i < runtime.lookup_count; ++i) {
        if (runtime.lookup_stages[i] == mutation_invalidation_prepare_stage) {
            has_prepare = true;
            break;
        }
    }
    EXPECT_TRUE(has_prepare);
}

TEST(MutationCacheInvalidationPipelineTest, MutationApplyStageInsertedForInvalidationRoute) {
    policy::RoutePolicy rt_policy{};
    rt_policy.cache.invalidation.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(rt_policy, &runtime);

    bool has_apply = false;
    for (std::size_t i = 0; i < runtime.response_count; ++i) {
        if (runtime.response_stages[i] == mutation_invalidation_apply_stage) {
            has_apply = true;
            break;
        }
    }
    EXPECT_TRUE(has_apply);
}

TEST(MutationCacheInvalidationPipelineTest, NonInvalidationRouteDoesNotGetMutationStages) {
    policy::RoutePolicy rt_policy{};
    rt_policy.cache.invalidation.enabled = false;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(rt_policy, &runtime);

    for (std::size_t i = 0; i < runtime.lookup_count; ++i) {
        EXPECT_NE(runtime.lookup_stages[i], mutation_invalidation_prepare_stage);
    }
    for (std::size_t i = 0; i < runtime.response_count; ++i) {
        EXPECT_NE(runtime.response_stages[i], mutation_invalidation_apply_stage);
    }
}

TEST(MutationCacheInvalidationPipelineTest, StageOrderIsCorrect) {
    policy::RoutePolicy rt_policy{};
    rt_policy.cache.invalidation.enabled = true;
    rt_policy.cache.behavior = policy::CacheBehavior::Store;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(rt_policy, &runtime);

    // Prepare stage should be before cache key prepare stage
    int prepare_idx = -1;
    int key_prepare_idx = -1;
    for (std::size_t i = 0; i < runtime.lookup_count; ++i) {
        if (runtime.lookup_stages[i] == mutation_invalidation_prepare_stage)
            prepare_idx = i;
        if (runtime.lookup_stages[i] == cache_key_prepare_stage)
            key_prepare_idx = i;
    }
    ASSERT_NE(prepare_idx, -1);
    ASSERT_NE(key_prepare_idx, -1);
    EXPECT_LT(prepare_idx, key_prepare_idx);

    // Apply stage should be before compression decision stage (which is always last/default)
    int apply_idx = -1;
    int compression_idx = -1;
    for (std::size_t i = 0; i < runtime.response_count; ++i) {
        if (runtime.response_stages[i] == mutation_invalidation_apply_stage)
            apply_idx = i;
        if (runtime.response_stages[i] == compression_decision_stage)
            compression_idx = i;
    }
    ASSERT_NE(apply_idx, -1);
    ASSERT_NE(compression_idx, -1);
    EXPECT_LT(apply_idx, compression_idx);
}

TEST(MutationCacheInvalidationPipelineTest, CompilationSucceedsWithAllFeaturesEnabled) {
    policy::RoutePolicy rt_policy{};
    rt_policy.cache.enabled = true;
    rt_policy.cache.behavior = policy::CacheBehavior::Store;
    rt_policy.cache.invalidation.enabled = true;
    rt_policy.coalescing.enabled = true;
    rt_policy.pagination.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(rt_policy, &runtime);

    // Assert prepare and apply stages are present among others
    bool has_prepare = false;
    bool has_cache_key = false;
    bool has_l1_lookup = false;
    bool has_coalescing_decision = false;
    bool has_pagination = false;

    for (std::size_t i = 0; i < runtime.lookup_count; ++i) {
        if (runtime.lookup_stages[i] == mutation_invalidation_prepare_stage)
            has_prepare = true;
        if (runtime.lookup_stages[i] == cache_key_prepare_stage)
            has_cache_key = true;
        if (runtime.lookup_stages[i] == l1_cache_lookup_stage)
            has_l1_lookup = true;
        if (runtime.lookup_stages[i] == coalescing_decision_stage)
            has_coalescing_decision = true;
        if (runtime.lookup_stages[i] == pagination_request_mutation_stage)
            has_pagination = true;
    }
    EXPECT_TRUE(has_prepare);
    EXPECT_TRUE(has_cache_key);
    EXPECT_TRUE(has_l1_lookup);
    EXPECT_TRUE(has_coalescing_decision);
    EXPECT_TRUE(has_pagination);

    bool has_apply = false;
    for (std::size_t i = 0; i < runtime.response_count; ++i) {
        if (runtime.response_stages[i] == mutation_invalidation_apply_stage)
            has_apply = true;
    }
    EXPECT_TRUE(has_apply);

    bool has_l1_store = false;
    bool has_coalescing_leader = false;
    for (std::size_t i = 0; i < runtime.store_count; ++i) {
        if (runtime.store_stages[i] == l1_cache_store_stage)
            has_l1_store = true;
        if (runtime.store_stages[i] == coalescing_leader_completion_stage)
            has_coalescing_leader = true;
    }
    EXPECT_TRUE(has_l1_store);
    EXPECT_TRUE(has_coalescing_leader);

    // Runtime should not exceed maximum stage capacity
    EXPECT_LE(runtime.lookup_count, kMaxLookupStages);
    EXPECT_LE(runtime.response_count, kMaxResponseStages);
}

} // namespace

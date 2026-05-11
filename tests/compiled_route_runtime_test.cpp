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
#include "stages/pagination_request_mutation_stage.h"

#include <gtest/gtest.h>

namespace bytetaper::extproc {

static bool contains_stage(const apg::ApgStage* stages, std::size_t count, apg::ApgStage stage) {
    for (std::size_t i = 0; i < count; ++i) {
        if (stages[i] == stage) {
            return true;
        }
    }
    return false;
}

TEST(CompiledRouteRuntimeTest, ExcludeCacheStages_WhenCacheDisabled) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Bypass; // disabled
    policy.coalescing.enabled = true;
    policy.pagination.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    EXPECT_FALSE(contains_stage(runtime.lookup_stages, runtime.lookup_count,
                                stages::cache_key_prepare_stage));
    EXPECT_FALSE(
        contains_stage(runtime.lookup_stages, runtime.lookup_count, stages::l1_cache_lookup_stage));
    EXPECT_FALSE(contains_stage(runtime.lookup_stages, runtime.lookup_count,
                                stages::l2_cache_async_lookup_enqueue_stage));

    EXPECT_FALSE(
        contains_stage(runtime.store_stages, runtime.store_count, stages::l1_cache_store_stage));
    EXPECT_FALSE(contains_stage(runtime.store_stages, runtime.store_count,
                                stages::l2_cache_async_store_enqueue_stage));

    // Coalescing and pagination must still be present
    EXPECT_TRUE(contains_stage(runtime.lookup_stages, runtime.lookup_count,
                               stages::coalescing_decision_stage));
    EXPECT_TRUE(contains_stage(runtime.lookup_stages, runtime.lookup_count,
                               stages::pagination_request_mutation_stage));
}

TEST(CompiledRouteRuntimeTest, ExcludeCoalescingStages_WhenCoalescingDisabled) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = false; // disabled
    policy.pagination.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    EXPECT_FALSE(contains_stage(runtime.lookup_stages, runtime.lookup_count,
                                stages::coalescing_decision_stage));
    EXPECT_FALSE(contains_stage(runtime.lookup_stages, runtime.lookup_count,
                                stages::coalescing_follower_wait_stage));
    EXPECT_FALSE(contains_stage(runtime.store_stages, runtime.store_count,
                                stages::coalescing_leader_completion_stage));

    // Cache must still be present
    EXPECT_TRUE(
        contains_stage(runtime.lookup_stages, runtime.lookup_count, stages::l1_cache_lookup_stage));
}

TEST(CompiledRouteRuntimeTest, ExcludePaginationStage_WhenPaginationDisabled) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = true;
    policy.pagination.enabled = false; // disabled

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    EXPECT_FALSE(contains_stage(runtime.lookup_stages, runtime.lookup_count,
                                stages::pagination_request_mutation_stage));
}

TEST(CompiledRouteRuntimeTest, PreserveOrdering_WhenAllFeaturesEnabled) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = true;
    policy.pagination.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    // Verify exact sequence of default pipelines is preserved in lookup
    ASSERT_EQ(runtime.lookup_count, 6);
    EXPECT_EQ(runtime.lookup_stages[0], stages::cache_key_prepare_stage);
    EXPECT_EQ(runtime.lookup_stages[1], stages::l1_cache_lookup_stage);
    EXPECT_EQ(runtime.lookup_stages[2], stages::coalescing_decision_stage);
    EXPECT_EQ(runtime.lookup_stages[3], stages::coalescing_follower_wait_stage);
    EXPECT_EQ(runtime.lookup_stages[4], stages::l2_cache_async_lookup_enqueue_stage);
    EXPECT_EQ(runtime.lookup_stages[5], stages::pagination_request_mutation_stage);

    // Verify exact sequence of default pipelines is preserved in store
    ASSERT_EQ(runtime.store_count, 3);
    EXPECT_EQ(runtime.store_stages[0], stages::l1_cache_store_stage);
    EXPECT_EQ(runtime.store_stages[1], stages::l2_cache_async_store_enqueue_stage);
    EXPECT_EQ(runtime.store_stages[2], stages::coalescing_leader_completion_stage);

    // Verify compression decision is always included in response
    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

TEST(CompiledRouteRuntimeTest, CompileTableAndLookup_FallbackBehavior) {
    policy::RoutePolicy policy1{};
    policy1.route_id = "route-1";
    policy1.cache.behavior = policy::CacheBehavior::Store;

    policy::RoutePolicy policy2{};
    policy2.route_id = "route-2";
    policy2.cache.behavior = policy::CacheBehavior::Bypass;

    policy::RoutePolicy policies[] = { policy1, policy2 };

    CompiledRouteRuntimeTable table{};
    compile_route_runtime_table(policies, 2, &table);

    EXPECT_EQ(table.count, 2);

    const auto* r1 = find_compiled_route_runtime(table, &policies[0]);
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->policy->route_id, std::string("route-1"));
    EXPECT_TRUE(contains_stage(r1->lookup_stages, r1->lookup_count, stages::l1_cache_lookup_stage));

    const auto* r2 = find_compiled_route_runtime(table, &policies[1]);
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->policy->route_id, std::string("route-2"));
    EXPECT_FALSE(
        contains_stage(r2->lookup_stages, r2->lookup_count, stages::l1_cache_lookup_stage));

    // Fallback: looking up unmatched policy pointer should return nullptr gracefully
    policy::RoutePolicy unmatched_policy{};
    unmatched_policy.route_id = "route-3";
    EXPECT_EQ(find_compiled_route_runtime(table, &unmatched_policy), nullptr);
    EXPECT_EQ(find_compiled_route_runtime(table, nullptr), nullptr);
}

TEST(CompiledRouteRuntimeTest, CacheOnly_HasExpectedStages) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.cache.field_variant.enabled = false;
    policy.coalescing.enabled = false;
    policy.pagination.enabled = false;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    ASSERT_EQ(runtime.lookup_count, 3);
    EXPECT_EQ(runtime.lookup_stages[0], stages::cache_key_prepare_stage);
    EXPECT_EQ(runtime.lookup_stages[1], stages::l1_cache_lookup_stage);
    EXPECT_EQ(runtime.lookup_stages[2], stages::l2_cache_async_lookup_enqueue_stage);

    ASSERT_EQ(runtime.store_count, 2);
    EXPECT_EQ(runtime.store_stages[0], stages::l1_cache_store_stage);
    EXPECT_EQ(runtime.store_stages[1], stages::l2_cache_async_store_enqueue_stage);

    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

TEST(CompiledRouteRuntimeTest, CacheAndCoalescing_HasExpectedStages) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.cache.field_variant.enabled = false;
    policy.coalescing.enabled = true;
    policy.pagination.enabled = false;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    ASSERT_EQ(runtime.lookup_count, 5);
    EXPECT_EQ(runtime.lookup_stages[0], stages::cache_key_prepare_stage);
    EXPECT_EQ(runtime.lookup_stages[1], stages::l1_cache_lookup_stage);
    EXPECT_EQ(runtime.lookup_stages[2], stages::coalescing_decision_stage);
    EXPECT_EQ(runtime.lookup_stages[3], stages::coalescing_follower_wait_stage);
    EXPECT_EQ(runtime.lookup_stages[4], stages::l2_cache_async_lookup_enqueue_stage);

    ASSERT_EQ(runtime.store_count, 3);
    EXPECT_EQ(runtime.store_stages[0], stages::l1_cache_store_stage);
    EXPECT_EQ(runtime.store_stages[1], stages::l2_cache_async_store_enqueue_stage);
    EXPECT_EQ(runtime.store_stages[2], stages::coalescing_leader_completion_stage);

    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

TEST(CompiledRouteRuntimeTest, CacheAndFieldVariant_HasExpectedStages) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.cache.field_variant.enabled = true;
    policy.coalescing.enabled = false;
    policy.pagination.enabled = false;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    ASSERT_EQ(runtime.lookup_count, 5);
    EXPECT_EQ(runtime.lookup_stages[0], stages::cache_key_prepare_stage);
    EXPECT_EQ(runtime.lookup_stages[1], stages::field_variant_admission_stage);
    EXPECT_EQ(runtime.lookup_stages[2], stages::l1_cache_lookup_stage);
    EXPECT_EQ(runtime.lookup_stages[3], stages::l1_variant_lookup_stage);
    EXPECT_EQ(runtime.lookup_stages[4], stages::l2_cache_async_lookup_enqueue_stage);

    ASSERT_EQ(runtime.store_count, 3);
    EXPECT_EQ(runtime.store_stages[0], stages::l1_cache_store_stage);
    EXPECT_EQ(runtime.store_stages[1], stages::l1_variant_store_stage);
    EXPECT_EQ(runtime.store_stages[2], stages::l2_cache_async_store_enqueue_stage);

    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

TEST(CompiledRouteRuntimeTest, PaginationOnly_HasExpectedStages) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Bypass;
    policy.coalescing.enabled = false;
    policy.pagination.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    ASSERT_EQ(runtime.lookup_count, 1);
    EXPECT_EQ(runtime.lookup_stages[0], stages::pagination_request_mutation_stage);

    ASSERT_EQ(runtime.store_count, 0);

    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

TEST(CompiledRouteRuntimeTest, NoFeatures_HasOnlyCompressionInResponse) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Bypass;
    policy.cache.field_variant.enabled = false;
    policy.coalescing.enabled = false;
    policy.pagination.enabled = false;
    policy.compression.enabled = false;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    ASSERT_EQ(runtime.lookup_count, 0);
    ASSERT_EQ(runtime.store_count, 0);

    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

TEST(CompiledRouteRuntimeTest, CompressionOnly_HasOnlyCompressionInResponse) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Bypass;
    policy.cache.field_variant.enabled = false;
    policy.coalescing.enabled = false;
    policy.pagination.enabled = false;
    policy.compression.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    ASSERT_EQ(runtime.lookup_count, 0);
    ASSERT_EQ(runtime.store_count, 0);

    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

TEST(CompiledRouteRuntimeTest, AllFeaturesWithFieldVariants_HasExpectedStages) {
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.cache.field_variant.enabled = true;
    policy.coalescing.enabled = true;
    policy.pagination.enabled = true;
    policy.compression.enabled = true;

    CompiledRouteRuntime runtime{};
    compile_route_runtime(policy, &runtime);

    ASSERT_EQ(runtime.lookup_count, 8);
    EXPECT_EQ(runtime.lookup_stages[0], stages::cache_key_prepare_stage);
    EXPECT_EQ(runtime.lookup_stages[1], stages::field_variant_admission_stage);
    EXPECT_EQ(runtime.lookup_stages[2], stages::l1_cache_lookup_stage);
    EXPECT_EQ(runtime.lookup_stages[3], stages::l1_variant_lookup_stage);
    EXPECT_EQ(runtime.lookup_stages[4], stages::coalescing_decision_stage);
    EXPECT_EQ(runtime.lookup_stages[5], stages::coalescing_follower_wait_stage);
    EXPECT_EQ(runtime.lookup_stages[6], stages::l2_cache_async_lookup_enqueue_stage);
    EXPECT_EQ(runtime.lookup_stages[7], stages::pagination_request_mutation_stage);

    ASSERT_EQ(runtime.store_count, 4);
    EXPECT_EQ(runtime.store_stages[0], stages::l1_cache_store_stage);
    EXPECT_EQ(runtime.store_stages[1], stages::l1_variant_store_stage);
    EXPECT_EQ(runtime.store_stages[2], stages::l2_cache_async_store_enqueue_stage);
    EXPECT_EQ(runtime.store_stages[3], stages::coalescing_leader_completion_stage);

    ASSERT_EQ(runtime.response_count, 1);
    EXPECT_EQ(runtime.response_stages[0], stages::compression_decision_stage);
}

} // namespace bytetaper::extproc

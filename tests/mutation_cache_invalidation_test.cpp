// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "metrics/cache_metrics.h"
#include "policy/route_policy.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/mutation_invalidation_apply_stage.h"
#include "stages/mutation_invalidation_prepare_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

using namespace bytetaper;
using namespace bytetaper::apg;
using namespace bytetaper::cache;
using namespace bytetaper::runtime;
using namespace bytetaper::stages;

namespace {

struct MutationInvalidationFixtureRoutes {
    policy::RoutePolicy get_user;
    policy::RoutePolicy patch_user;
    policy::RoutePolicy put_user;
    policy::RoutePolicy delete_user;
    policy::RoutePolicy routes[4];
};

void configure_cacheable_get_route(policy::RoutePolicy* route, const char* route_id) {
    *route = policy::RoutePolicy{};
    route->route_id = route_id;
    route->allowed_method = policy::HttpMethod::Get;
    route->cache.enabled = true;
    route->cache.behavior = policy::CacheBehavior::Store;
}

void configure_mutation_route(policy::RoutePolicy* route, const char* route_id,
                              policy::HttpMethod method, const char* target_route_id) {
    *route = policy::RoutePolicy{};
    route->route_id = route_id;
    route->allowed_method = method;
    route->cache.invalidation.enabled = true;
    route->cache.invalidation.target_count = 1;
    std::strncpy(route->cache.invalidation.targets[0].route_id, target_route_id,
                 sizeof(route->cache.invalidation.targets[0].route_id) - 1);
    route->cache.invalidation.targets[0].strategy = policy::CacheInvalidationStrategy::RouteEpoch;

    switch (method) {
    case policy::HttpMethod::Patch:
        route->cache.invalidation.on_patch = true;
        break;
    case policy::HttpMethod::Put:
        route->cache.invalidation.on_put = true;
        break;
    case policy::HttpMethod::Delete:
        route->cache.invalidation.on_delete = true;
        break;
    default:
        break;
    }
}

void register_cacheable_routes(RouteCacheEpochStore* store, const policy::RoutePolicy* routes,
                               std::size_t route_count) {
    for (std::size_t i = 0; i < route_count; ++i) {
        if (routes[i].cache.enabled || routes[i].cache.invalidation.enabled) {
            route_cache_epoch_register(store, routes[i].route_id);
        }
    }
}

static std::uint64_t epoch_store_get_epoch_helper(const RouteCacheEpochStore* store,
                                                  const char* route_id) {
    std::uint64_t epoch = 0;
    route_cache_epoch_get(store, route_id, &epoch);
    return epoch;
}

void run_prepare_and_apply(ApgTransformContext* ctx, std::uint16_t response_status_code) {
    mutation_invalidation_prepare_stage(*ctx);
    ctx->response_status_code = response_status_code;
    mutation_invalidation_apply_stage(*ctx);
}

std::string build_raw_get_key(const policy::RoutePolicy& route, RouteCacheEpochStore* epoch_store,
                              const char* path) {
    CacheKeyInput input{};
    input.method = policy::HttpMethod::Get;
    input.route_id = route.route_id;
    input.path = path;
    input.route_cache_epoch = epoch_store_get_epoch_helper(epoch_store, route.route_id);
    input.route_cache_epoch_ready = true;

    char buf[kCacheKeyMaxLen];
    if (build_cache_key(input, buf, sizeof(buf))) {
        return buf;
    }
    return "";
}

class MutationCacheInvalidationTest : public ::testing::Test {
protected:
    MutationInvalidationFixtureRoutes fr;
    RouteCacheEpochStore epoch_store{};
    L1Cache l1_store{};
    L1Cache* l1 = &l1_store;
    bytetaper::metrics::CacheMetrics metrics{};
    ApgTransformContext context{};

    void SetUp() override {
        configure_cacheable_get_route(&fr.get_user, "get_user");
        configure_mutation_route(&fr.patch_user, "patch_user", policy::HttpMethod::Patch,
                                 "get_user");
        configure_mutation_route(&fr.put_user, "put_user", policy::HttpMethod::Put, "get_user");
        configure_mutation_route(&fr.delete_user, "delete_user", policy::HttpMethod::Delete,
                                 "get_user");

        fr.routes[0] = fr.get_user;
        fr.routes[1] = fr.patch_user;
        fr.routes[2] = fr.put_user;
        fr.routes[3] = fr.delete_user;

        epoch_store.count = 0;
        register_cacheable_routes(&epoch_store, fr.routes, 4);

        l1_init(l1);

        context = ApgTransformContext{};
        context.active_routes = fr.routes;
        context.active_route_count = 4;
        context.route_cache_epoch_store = &epoch_store;
        context.l1_cache = l1;
        context.cache_metrics = &metrics;
    }

    void TearDown() override {
        // RocksDB based L2 would need cleanup if used, but we are L1 focused here.
    }
};

// --- 1. Policy and resolver integration ---

TEST_F(MutationCacheInvalidationTest, PatchPolicyResolvesGetTarget) {
    context.matched_policy = &fr.patch_user;
    context.request_method = policy::HttpMethod::Patch;

    auto res = mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(res.result, StageResult::Continue);
    EXPECT_TRUE(context.mutation_invalidation.prepared);
    EXPECT_EQ(context.mutation_invalidation.target_count, 1u);
    EXPECT_STREQ(context.mutation_invalidation.targets[0].route_id, "get_user");
    EXPECT_EQ(metrics.invalidation_prepare_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_prepared_total.load(), 1u);
}

TEST_F(MutationCacheInvalidationTest, PutPolicyResolvesGetTarget) {
    context.matched_policy = &fr.put_user;
    context.request_method = policy::HttpMethod::Put;

    auto res = mutation_invalidation_prepare_stage(context);
    EXPECT_TRUE(context.mutation_invalidation.prepared);
}

TEST_F(MutationCacheInvalidationTest, DeletePolicyResolvesGetTarget) {
    context.matched_policy = &fr.delete_user;
    context.request_method = policy::HttpMethod::Delete;

    auto res = mutation_invalidation_prepare_stage(context);
    EXPECT_TRUE(context.mutation_invalidation.prepared);
}

TEST_F(MutationCacheInvalidationTest, UnknownTargetRouteDoesNotPreparePlan) {
    policy::RoutePolicy broken_patch;
    configure_mutation_route(&broken_patch, "broken_patch", policy::HttpMethod::Patch, "ghost");
    context.matched_policy = &broken_patch;
    context.request_method = policy::HttpMethod::Patch;

    mutation_invalidation_prepare_stage(context);
    EXPECT_FALSE(context.mutation_invalidation.prepared);
    EXPECT_EQ(metrics.invalidation_prepare_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_target_resolve_failed_total.load(), 1u);
}

TEST_F(MutationCacheInvalidationTest, NonCacheableTargetDoesNotPreparePlan) {
    policy::RoutePolicy target_no_cache;
    configure_cacheable_get_route(&target_no_cache, "no_cache");
    target_no_cache.cache.enabled = false;

    policy::RoutePolicy patch_no_cache;
    configure_mutation_route(&patch_no_cache, "patch_nc", policy::HttpMethod::Patch, "no_cache");

    policy::RoutePolicy routes[2] = { target_no_cache, patch_no_cache };
    context.active_routes = routes;
    context.active_route_count = 2;
    context.matched_policy = &patch_no_cache;
    context.request_method = policy::HttpMethod::Patch;

    mutation_invalidation_prepare_stage(context);
    EXPECT_FALSE(context.mutation_invalidation.prepared);
}

TEST_F(MutationCacheInvalidationTest, MutationWithoutInvalidationPolicyDoesNotPrepare) {
    policy::RoutePolicy no_inv_policy;
    configure_cacheable_get_route(&no_inv_policy, "no_inv");
    no_inv_policy.allowed_method = policy::HttpMethod::Patch;
    no_inv_policy.cache.invalidation.enabled = false;

    context.matched_policy = &no_inv_policy;
    context.request_method = policy::HttpMethod::Patch;

    mutation_invalidation_prepare_stage(context);
    EXPECT_FALSE(context.mutation_invalidation.prepared);
}

TEST_F(MutationCacheInvalidationTest, NonGetTargetDoesNotPreparePlan) {
    policy::RoutePolicy target_post;
    configure_cacheable_get_route(&target_post, "post_target");
    target_post.allowed_method = policy::HttpMethod::Post;

    policy::RoutePolicy patch_to_post;
    configure_mutation_route(&patch_to_post, "patch_to_post", policy::HttpMethod::Patch,
                             "post_target");

    policy::RoutePolicy routes[2] = { target_post, patch_to_post };
    context.active_routes = routes;
    context.active_route_count = 2;
    context.matched_policy = &patch_to_post;
    context.request_method = policy::HttpMethod::Patch;

    mutation_invalidation_prepare_stage(context);
    EXPECT_FALSE(context.mutation_invalidation.prepared);
}

// --- 2. Prepare/apply stage integration ---

TEST_F(MutationCacheInvalidationTest, PatchPrepareThenSuccessfulResponseBumpsEpoch) {
    context.matched_policy = &fr.patch_user;
    context.request_method = policy::HttpMethod::Patch;

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);

    run_prepare_and_apply(&context, 204);

    EXPECT_TRUE(context.mutation_invalidation.applied);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);
    EXPECT_EQ(metrics.invalidation_applied_total.load(), 1u);
    EXPECT_EQ(metrics.route_epoch_bump_success_total.load(), 1u);
}

TEST_F(MutationCacheInvalidationTest, PutPrepareThenSuccessfulResponseBumpsEpoch) {
    context.matched_policy = &fr.put_user;
    context.request_method = policy::HttpMethod::Put;

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);

    run_prepare_and_apply(&context, 200);

    EXPECT_TRUE(context.mutation_invalidation.applied);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);
}

TEST_F(MutationCacheInvalidationTest, DeletePrepareThenSuccessfulResponseBumpsEpoch) {
    context.matched_policy = &fr.delete_user;
    context.request_method = policy::HttpMethod::Delete;

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);

    run_prepare_and_apply(&context, 204);

    EXPECT_TRUE(context.mutation_invalidation.applied);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);
}

TEST_F(MutationCacheInvalidationTest, IdempotentApplyForSamePreparedPlan) {
    context.matched_policy = &fr.patch_user;
    context.request_method = policy::HttpMethod::Patch;

    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 204;

    // Apply first time
    mutation_invalidation_apply_stage(context);
    EXPECT_TRUE(context.mutation_invalidation.applied);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);

    // Apply second time (should be idempotent for the same context/plan)
    // Actually, in our current implementation, apply_stage marks 'applied = true'
    // and returns if already applied.
    mutation_invalidation_apply_stage(context);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);
}

TEST_F(MutationCacheInvalidationTest, MultipleTargetsBumpTogether) {
    policy::RoutePolicy t1, t2;
    configure_cacheable_get_route(&t1, "t1");
    configure_cacheable_get_route(&t2, "t2");
    route_cache_epoch_register(&epoch_store, "t1");
    route_cache_epoch_register(&epoch_store, "t2");

    policy::RoutePolicy multi_patch;
    configure_mutation_route(&multi_patch, "multi_patch", policy::HttpMethod::Patch, "t1");
    multi_patch.cache.invalidation.target_count = 2;
    std::strcpy(multi_patch.cache.invalidation.targets[1].route_id, "t2");
    multi_patch.cache.invalidation.targets[1].strategy =
        policy::CacheInvalidationStrategy::RouteEpoch;

    policy::RoutePolicy routes[3] = { t1, t2, multi_patch };
    context.active_routes = routes;
    context.active_route_count = 3;
    context.matched_policy = &multi_patch;
    context.request_method = policy::HttpMethod::Patch;

    run_prepare_and_apply(&context, 204);

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "t1"), 2u);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "t2"), 2u);
}

TEST_F(MutationCacheInvalidationTest, FailedMutationResponseDoesNotBumpEpoch) {
    context.matched_policy = &fr.patch_user;
    context.request_method = policy::HttpMethod::Patch;

    run_prepare_and_apply(&context, 500);

    EXPECT_FALSE(context.mutation_invalidation.applied);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);
    EXPECT_EQ(metrics.invalidation_skipped_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_status_not_successful_total.load(), 1u);
}

TEST_F(MutationCacheInvalidationTest, Patch404DoesNotBumpEpoch) {
    context.matched_policy = &fr.patch_user;
    context.request_method = policy::HttpMethod::Patch;

    run_prepare_and_apply(&context, 404);

    EXPECT_FALSE(context.mutation_invalidation.applied);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);
}

// --- 3. L1 raw cache invalidation ---

TEST_F(MutationCacheInvalidationTest, L1RawCacheMissesAfterPatchEpochBump) {
    // 1. Store old body in L1 under epoch 1
    std::string key1 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "old_body";
    entry.body_len = 8;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    // Verify it hits
    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(l1, key1.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));

    // 2. Perform mutation (epoch 1 -> 2)
    context.matched_policy = &fr.patch_user;
    context.request_method = policy::HttpMethod::Patch;
    run_prepare_and_apply(&context, 204);
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);

    // 3. New key misses
    std::string key2 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    EXPECT_NE(key1, key2);
    EXPECT_FALSE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));

    // 4. Store fresh body under epoch 2
    entry.body = "new_body";
    std::strncpy(entry.key, key2.c_str(), sizeof(entry.key) - 1);
    l1_put(l1, entry);
    EXPECT_TRUE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
    EXPECT_EQ(std::string(hit_body, hit.body_len), "new_body");
}

TEST_F(MutationCacheInvalidationTest, L1RawCacheMissesAfterPutEpochBump) {
    std::string key1 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "old";
    entry.body_len = 3;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    context.matched_policy = &fr.put_user;
    context.request_method = policy::HttpMethod::Put;
    run_prepare_and_apply(&context, 200);

    std::string key2 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_FALSE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
}

TEST_F(MutationCacheInvalidationTest, L1RawCacheMissesAfterDeleteEpochBump) {
    std::string key1 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "old";
    entry.body_len = 3;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    context.matched_policy = &fr.delete_user;
    context.request_method = policy::HttpMethod::Delete;
    run_prepare_and_apply(&context, 204);

    std::string key2 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_FALSE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
}

TEST_F(MutationCacheInvalidationTest, L1RawCacheRemainsValidAfterFailedMutation) {
    std::string key1 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "still_valid";
    entry.body_len = 11;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    context.matched_policy = &fr.patch_user;
    context.request_method = policy::HttpMethod::Patch;
    run_prepare_and_apply(&context, 500);

    std::string key2 = build_raw_get_key(fr.get_user, &epoch_store, "/users/1");
    EXPECT_EQ(key1, key2); // Epoch didn't change

    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
    EXPECT_EQ(std::string(hit_body, hit.body_len), "still_valid");
}

} // namespace

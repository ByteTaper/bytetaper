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

using namespace bytetaper;
using namespace bytetaper::apg;
using namespace bytetaper::cache;
using namespace bytetaper::runtime;
using namespace bytetaper::stages;

namespace {

void configure_cacheable_get_route(policy::RoutePolicy* route, const char* route_id) {
    *route = policy::RoutePolicy{};
    route->route_id = route_id;
    route->allowed_method = policy::HttpMethod::Get;
    route->cache.enabled = true;
    route->cache.behavior = policy::CacheBehavior::Store;
    route->cache.field_variant.enabled = true;
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

static std::uint64_t epoch_store_get_epoch_helper(const RouteCacheEpochStore* store,
                                                  const char* route_id) {
    std::uint64_t epoch = 0;
    route_cache_epoch_get(store, route_id, &epoch);
    return epoch;
}

std::string build_variant_get_key(const policy::RoutePolicy& route,
                                  RouteCacheEpochStore* epoch_store, const char* path,
                                  const char* field) {
    CacheKeyInput input{};
    input.method = policy::HttpMethod::Get;
    input.route_id = route.route_id;
    input.path = path;
    input.route_cache_epoch = epoch_store_get_epoch_helper(epoch_store, route.route_id);
    input.route_cache_epoch_ready = true;
    input.variant = true;

    static char field_arr[1][policy::kMaxFieldNameLen];
    std::strncpy(field_arr[0], field, policy::kMaxFieldNameLen - 1);
    input.selected_fields = field_arr;
    input.selected_field_count = 1;

    char buf[kCacheKeyMaxLen];
    if (build_cache_key(input, buf, sizeof(buf))) {
        return buf;
    }
    return "";
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

class MutationCacheInvalidationVariantTest : public ::testing::Test {
protected:
    RouteCacheEpochStore epoch_store{};
    L1Cache l1_store{};
    L1Cache* l1 = &l1_store;
    bytetaper::metrics::CacheMetrics metrics{};
    policy::RoutePolicy routes[4];
    ApgTransformContext context{};

    void SetUp() override {
        configure_cacheable_get_route(&routes[0], "get_user");
        configure_mutation_route(&routes[1], "patch_user", policy::HttpMethod::Patch, "get_user");
        configure_mutation_route(&routes[2], "put_user", policy::HttpMethod::Put, "get_user");
        configure_mutation_route(&routes[3], "delete_user", policy::HttpMethod::Delete, "get_user");

        epoch_store.count = 0;
        route_cache_epoch_register(&epoch_store, "get_user");
        route_cache_epoch_register(&epoch_store, "patch_user");
        route_cache_epoch_register(&epoch_store, "put_user");
        route_cache_epoch_register(&epoch_store, "delete_user");

        l1_init(l1);

        context = ApgTransformContext{};
        context.active_routes = routes;
        context.active_route_count = 4;
        context.route_cache_epoch_store = &epoch_store;
        context.l1_cache = l1;
        context.cache_metrics = &metrics;
    }

    void TearDown() override {}
};

TEST_F(MutationCacheInvalidationVariantTest, VariantCacheMissesAfterPatchEpochBump) {
    // 1. Store variant in L1 under epoch 1
    std::string key1 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "name");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "{\"name\":\"alice\"}";
    entry.body_len = 16;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    // Verify hit
    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(l1, key1.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));

    // 2. Mutation (epoch 1 -> 2)
    context.matched_policy = &routes[1];
    context.request_method = policy::HttpMethod::Patch;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 204;
    mutation_invalidation_apply_stage(context);

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);

    // 3. New variant key misses
    std::string key2 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "name");
    EXPECT_NE(key1, key2);
    EXPECT_FALSE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
}

TEST_F(MutationCacheInvalidationVariantTest, VariantCacheMissesAfterPutEpochBump) {
    std::string key1 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "age");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "{\"age\":30}";
    entry.body_len = 10;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    context.matched_policy = &routes[2];
    context.request_method = policy::HttpMethod::Put;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 200;
    mutation_invalidation_apply_stage(context);

    std::string key2 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "age");
    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_FALSE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
}

TEST_F(MutationCacheInvalidationVariantTest, VariantCacheMissesAfterDeleteEpochBump) {
    std::string key1 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "name");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "{\"name\":\"x\"}";
    entry.body_len = 12;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    context.matched_policy = &routes[3];
    context.request_method = policy::HttpMethod::Delete;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 204;
    mutation_invalidation_apply_stage(context);

    std::string key2 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "name");
    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_FALSE(l1_get(l1, key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
}

TEST_F(MutationCacheInvalidationVariantTest, VariantCacheRemainsValidAfterFailedMutation) {
    std::string key1 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "name");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "{\"name\":\"alice\"}";
    entry.body_len = 16;
    entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, entry);

    context.matched_policy = &routes[1];
    context.request_method = policy::HttpMethod::Patch;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 404;
    mutation_invalidation_apply_stage(context);

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);

    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(l1, key1.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
}

TEST_F(MutationCacheInvalidationVariantTest, PatchSuccessInvalidatesRawAndVariantCachesByEpoch) {
    // 1. Store raw entry
    std::string raw_key1 = build_raw_get_key(routes[0], &epoch_store, "/users/1");
    CacheEntry raw_entry{};
    std::strncpy(raw_entry.key, raw_key1.c_str(), sizeof(raw_entry.key) - 1);
    raw_entry.body = "full_json";
    raw_entry.body_len = 9;
    raw_entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, raw_entry);

    // 2. Store variant entry
    std::string var_key1 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "name");
    CacheEntry var_entry{};
    std::strncpy(var_entry.key, var_key1.c_str(), sizeof(var_entry.key) - 1);
    var_entry.body = "name_only";
    var_entry.body_len = 9;
    var_entry.expires_at_epoch_ms = 9999999999;
    l1_put(l1, var_entry);

    // Verify both hit
    CacheEntry hit;
    char hit_body[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(l1, raw_key1.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
    EXPECT_TRUE(l1_get(l1, var_key1.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));

    // 3. Mutation
    context.matched_policy = &routes[1];
    context.request_method = policy::HttpMethod::Patch;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 204;
    mutation_invalidation_apply_stage(context);

    // 4. Verify both miss due to new epoch
    std::string raw_key2 = build_raw_get_key(routes[0], &epoch_store, "/users/1");
    std::string var_key2 = build_variant_get_key(routes[0], &epoch_store, "/users/1", "name");

    EXPECT_NE(raw_key1, raw_key2);
    EXPECT_NE(var_key1, var_key2);

    EXPECT_FALSE(l1_get(l1, raw_key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
    EXPECT_FALSE(l1_get(l1, var_key2.c_str(), 1500, &hit, hit_body, sizeof(hit_body)));
}

TEST_F(MutationCacheInvalidationVariantTest, RawAndVariantUseSameBumpedEpoch) {
    // 1. Start at epoch 1
    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);

    // 2. Perform mutation
    context.matched_policy = &routes[1];
    context.request_method = policy::HttpMethod::Patch;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 204;
    mutation_invalidation_apply_stage(context);

    // 3. Both raw and variant keys should now use epoch 2
    std::uint64_t bumped_epoch = epoch_store_get_epoch_helper(&epoch_store, "get_user");
    EXPECT_EQ(bumped_epoch, 2u);

    // Verify manually that CacheKeyInput would see the same epoch for both
    CacheKeyInput raw_input{};
    raw_input.route_id = "get_user";
    raw_input.route_cache_epoch = epoch_store_get_epoch_helper(&epoch_store, "get_user");
    raw_input.variant = false;

    CacheKeyInput var_input{};
    var_input.route_id = "get_user";
    var_input.route_cache_epoch = epoch_store_get_epoch_helper(&epoch_store, "get_user");
    var_input.variant = true;

    EXPECT_EQ(raw_input.route_cache_epoch, bumped_epoch);
    EXPECT_EQ(var_input.route_cache_epoch, bumped_epoch);
    EXPECT_EQ(raw_input.route_cache_epoch, var_input.route_cache_epoch);
}

} // namespace

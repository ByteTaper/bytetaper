// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "cache/cache_key.h"
#include "cache/l2_disk_cache.h"
#include "metrics/cache_metrics.h"
#include "policy/route_policy.h"
#include "runtime/route_cache_epoch_store.h"
#include "runtime/worker_queue.h"
#include "stages/mutation_invalidation_apply_stage.h"
#include "stages/mutation_invalidation_prepare_stage.h"

#include <cstring>
#include <filesystem>
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

class MutationCacheInvalidationL2Test : public ::testing::Test {
protected:
    std::string db_path = "test_mutation_l2_db";
    L2DiskCache* l2 = nullptr;
    RouteCacheEpochStore epoch_store{};
    bytetaper::metrics::CacheMetrics metrics{};
    policy::RoutePolicy routes[2];
    ApgTransformContext context{};

    void SetUp() override {
        std::filesystem::remove_all(db_path);
        l2 = l2_open(db_path.c_str());
        ASSERT_NE(l2, nullptr);

        configure_cacheable_get_route(&routes[0], "get_user");
        configure_mutation_route(&routes[1], "patch_user", policy::HttpMethod::Patch, "get_user");

        epoch_store.count = 0;
        route_cache_epoch_register(&epoch_store, "get_user");
        route_cache_epoch_register(&epoch_store, "patch_user");

        context = ApgTransformContext{};
        context.active_routes = routes;
        context.active_route_count = 2;
        context.route_cache_epoch_store = &epoch_store;
        context.l2_cache = l2;
        context.cache_metrics = &metrics;
    }

    void TearDown() override {
        if (l2) {
            l2_close(&l2);
        }
        std::filesystem::remove_all(db_path);
    }
};

TEST_F(MutationCacheInvalidationL2Test, L2OldEpochEntryIsUnreachableAfterEpochBump) {
    // 1. Store old body in L2 under epoch 1
    std::string key1 = build_raw_get_key(routes[0], &epoch_store, "/users/1");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "old_l2_body";
    entry.body_len = 11;
    entry.expires_at_epoch_ms = 9999999999;
    char enc_buf[2048];
    ASSERT_EQ(l2_put_result(l2, entry, enc_buf, sizeof(enc_buf)), L2PutResult::Stored);

    // Verify it hits
    CacheEntry hit;
    char scratch[2048];
    EXPECT_EQ(l2_get_result(l2, key1.c_str(), 1500, &hit, scratch, sizeof(scratch)),
              L2GetResult::Hit);

    // 2. Perform mutation (epoch 1 -> 2)
    context.matched_policy = &routes[1];
    context.request_method = policy::HttpMethod::Patch;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 204;
    mutation_invalidation_apply_stage(context);

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 2u);

    // 3. New key misses in L2
    std::string key2 = build_raw_get_key(routes[0], &epoch_store, "/users/1");
    EXPECT_NE(key1, key2);
    EXPECT_EQ(l2_get_result(l2, key2.c_str(), 1500, &hit, scratch, sizeof(scratch)),
              L2GetResult::Miss);

    // 4. Store fresh body under epoch 2
    entry.body = "new_l2_body";
    std::strncpy(entry.key, key2.c_str(), sizeof(entry.key) - 1);
    ASSERT_EQ(l2_put_result(l2, entry, enc_buf, sizeof(enc_buf)), L2PutResult::Stored);
    EXPECT_EQ(l2_get_result(l2, key2.c_str(), 1500, &hit, scratch, sizeof(scratch)),
              L2GetResult::Hit);
    EXPECT_EQ(std::string(hit.body, hit.body_len), "new_l2_body");
}

TEST_F(MutationCacheInvalidationL2Test, L2EntryRemainsValidAfterFailedMutation) {
    std::string key1 = build_raw_get_key(routes[0], &epoch_store, "/users/1");
    CacheEntry entry{};
    std::strncpy(entry.key, key1.c_str(), sizeof(entry.key) - 1);
    entry.body = "l2_still_valid";
    entry.body_len = 14;
    entry.expires_at_epoch_ms = 9999999999;
    char enc_buf[2048];
    ASSERT_EQ(l2_put_result(l2, entry, enc_buf, sizeof(enc_buf)), L2PutResult::Stored);

    context.matched_policy = &routes[1];
    context.request_method = policy::HttpMethod::Patch;
    mutation_invalidation_prepare_stage(context);
    context.response_status_code = 500;
    mutation_invalidation_apply_stage(context);

    EXPECT_EQ(epoch_store_get_epoch_helper(&epoch_store, "get_user"), 1u);

    CacheEntry hit;
    char scratch[2048];
    EXPECT_EQ(l2_get_result(l2, key1.c_str(), 1500, &hit, scratch, sizeof(scratch)),
              L2GetResult::Hit);
    EXPECT_EQ(std::string(hit.body, hit.body_len), "l2_still_valid");
}

} // namespace

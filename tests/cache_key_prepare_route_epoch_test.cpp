// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "policy/route_policy.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/cache_key_prepare_stage.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::stages {

class CacheKeyPrepareRouteEpochTest : public ::testing::Test {
protected:
    apg::ApgTransformContext ctx{};
    policy::RoutePolicy policy{};
    runtime::RouteCacheEpochStore store{};

    void SetUp() override {
        ctx = {};
        policy = {};
        store.count = 0;

        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.route_id = "route-1";

        ctx.matched_policy = &policy;
        ctx.request_method = policy::HttpMethod::Get;
        std::strncpy(ctx.raw_path, "/test", sizeof(ctx.raw_path) - 1);
    }
};

TEST_F(CacheKeyPrepareRouteEpochTest, ReadsEpochForCacheableRoute) {
    runtime::route_cache_epoch_register(&store, "route-1");
    runtime::route_cache_epoch_bump(&store, "route-1", nullptr); // epoch 2

    ctx.route_cache_epoch_store = &store;

    auto out = cache_key_prepare_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_TRUE(ctx.route_cache_epoch_ready);
    EXPECT_EQ(ctx.route_cache_epoch, 2);
    EXPECT_TRUE(std::strstr(ctx.cache_key, "epoch:2") != nullptr);
}

TEST_F(CacheKeyPrepareRouteEpochTest, BuildsVariantKeyWithSameEpoch) {
    policy.cache.field_variant.enabled = true;
    runtime::route_cache_epoch_register(&store, "route-1");
    ctx.route_cache_epoch_store = &store;

    cache_key_prepare_stage(ctx);

    EXPECT_TRUE(ctx.cache_key_ready);
    EXPECT_TRUE(ctx.variant_cache_key_ready);
    EXPECT_TRUE(std::strstr(ctx.cache_key, "epoch:1") != nullptr);
    EXPECT_TRUE(std::strstr(ctx.variant_cache_key, "epoch:1") != nullptr);
}

TEST_F(CacheKeyPrepareRouteEpochTest, BypassesCacheWhenEpochStoreMissing) {
    ctx.route_cache_epoch_store = nullptr;

    auto out = cache_key_prepare_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "route-epoch-not-ready");
    EXPECT_FALSE(ctx.cache_eligible);
    EXPECT_FALSE(ctx.route_cache_epoch_ready);
    EXPECT_TRUE(std::strstr(ctx.cache_key, "epoch:") == nullptr);
}

TEST_F(CacheKeyPrepareRouteEpochTest, BypassesCacheWhenRouteEpochMissing) {
    // Store is present, but "route-1" is NOT registered
    ctx.route_cache_epoch_store = &store;

    auto out = cache_key_prepare_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "route-epoch-not-ready");
    EXPECT_FALSE(ctx.cache_eligible);
}

} // namespace bytetaper::stages

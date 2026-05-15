// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/l1_cache_lookup_stage.h"
#include "stages/l2_cache_lookup_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

static const char* kTestDbPath = "/tmp/bytetaper_l2_promotion_test";

class L2PromotionTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache::l2_destroy(kTestDbPath);
        l2_ = cache::l2_open(kTestDbPath);
        ASSERT_NE(l2_, nullptr);
        l1_ = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1_.get());
        store_.count = 0;
        runtime::route_cache_epoch_register(&store_, "rt1");
    }

    void TearDown() override {
        cache::l2_close(&l2_);
        cache::l2_destroy(kTestDbPath);
    }

    cache::L2DiskCache* setup_l2_entry(apg::ApgTransformContext* ctx, policy::RoutePolicy* pol,
                                       const char* path, std::int64_t expires_at) {
        cache::CacheKeyInput ki{};
        ki.method = policy::HttpMethod::Get;
        ki.route_id = pol->route_id;
        ki.path = path;
        ki.policy_version = pol->route_id;
        ki.route_cache_epoch = 1;
        ki.route_cache_epoch_ready = true;
        char key_buf[cache::kCacheKeyMaxLen] = {};
        cache::build_cache_key(ki, key_buf, sizeof(key_buf));

        cache::CacheEntry e{};
        std::strncpy(e.key, key_buf, cache::kCacheKeyMaxLen - 1);
        e.status_code = 200;
        e.expires_at_epoch_ms = expires_at;
        const char body[] = "cached";
        e.body = body;
        e.body_len = 6;
        cache::l2_put(l2_, e);

        ctx->matched_policy = pol;
        ctx->l2_cache = l2_;
        ctx->l1_cache = l1_.get();
        ctx->route_cache_epoch_store = &store_;
        std::strncpy(ctx->raw_path, path, sizeof(ctx->raw_path) - 1);
        return l2_;
    }

    cache::L2DiskCache* l2_ = nullptr;
    std::unique_ptr<cache::L1Cache> l1_;
    runtime::RouteCacheEpochStore store_;
};

TEST_F(L2PromotionTest, L2HitPromotesToL1) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    setup_l2_entry(&ctx, &pol, "/api/items", 9999999);
    ctx.request_method = policy::HttpMethod::Get;

    // Verify L1 is empty
    cache::CacheEntry check{};

    cache::CacheKeyInput ki{};
    ki.method = policy::HttpMethod::Get;
    ki.route_id = pol.route_id;
    ki.path = "/api/items";
    ki.policy_version = pol.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    cache::build_cache_key(ki, key_buf, sizeof(key_buf));
    char body_buf[cache::kL1MaxBodySize];
    EXPECT_FALSE(cache::l1_get(l1_.get(), key_buf, 0, &check, body_buf, sizeof(body_buf)));

    // First request hits L2 and should promote
    cache_key_prepare_stage(ctx);
    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::SkipRemaining);
    EXPECT_STREQ(ctx.cache_layer, "L2");

    // Verify now in L1
    EXPECT_TRUE(cache::l1_get(l1_.get(), key_buf, 0, &check, body_buf, sizeof(body_buf)));
    EXPECT_EQ(check.status_code, 200);
}

TEST_F(L2PromotionTest, SecondRequestHitsL1AfterPromotion) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    setup_l2_entry(&ctx, &pol, "/api/items", 9999999);
    ctx.request_method = policy::HttpMethod::Get;
    ctx.request_epoch_ms = 1000;

    // First request: L2 hit + promotion
    cache_key_prepare_stage(ctx);
    l2_cache_lookup_stage(ctx);
    EXPECT_STREQ(ctx.cache_layer, "L2");

    // Second request: same L1 cache, fresh context
    apg::ApgTransformContext ctx2{};
    ctx2.matched_policy = &pol;
    ctx2.l1_cache = l1_.get();
    ctx2.route_cache_epoch_store = &store_;
    ctx2.request_method = policy::HttpMethod::Get;
    ctx2.request_epoch_ms = 1000;
    std::strncpy(ctx2.raw_path, "/api/items", sizeof(ctx2.raw_path) - 1);

    cache_key_prepare_stage(ctx2);
    auto out2 = l1_cache_lookup_stage(ctx2);
    EXPECT_EQ(out2.result, apg::StageResult::SkipRemaining);
    EXPECT_STREQ(ctx2.cache_layer, "L1");
    EXPECT_TRUE(ctx2.cache_hit);
}

TEST_F(L2PromotionTest, ExpiredL2NotPromoted) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    setup_l2_entry(&ctx, &pol, "/api/items", 500); // Expired
    ctx.request_method = policy::HttpMethod::Get;
    ctx.request_epoch_ms = 1000;

    cache_key_prepare_stage(ctx);
    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);

    // Verify L1 is still empty
    cache::CacheKeyInput ki{};
    ki.method = policy::HttpMethod::Get;
    ki.route_id = pol.route_id;
    ki.path = "/api/items";
    ki.policy_version = pol.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    cache::build_cache_key(ki, key_buf, sizeof(key_buf));

    cache::CacheEntry check{};
    char body_buf[cache::kL1MaxBodySize];
    EXPECT_FALSE(cache::l1_get(l1_.get(), key_buf, 1000, &check, body_buf, sizeof(body_buf)));
}

TEST_F(L2PromotionTest, NullL1CacheSkipsPromotion) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    setup_l2_entry(&ctx, &pol, "/api/items", 9999999);
    ctx.request_method = policy::HttpMethod::Get;
    ctx.route_cache_epoch_store = &store_;
    ctx.l1_cache = nullptr; // Explicitly null

    cache_key_prepare_stage(ctx);
    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::SkipRemaining);
    EXPECT_STREQ(ctx.cache_layer, "L2");
    // No crash, logic continues
}

} // namespace bytetaper::stages

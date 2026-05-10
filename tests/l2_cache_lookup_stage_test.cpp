// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l2_disk_cache.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/l2_cache_lookup_stage.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::stages {

static const char* kTestDbPath = "/tmp/bytetaper_l2_lookup_stage_test";

class L2CacheLookupStageTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache::l2_destroy(kTestDbPath);
        l2_ = cache::l2_open(kTestDbPath);
        ASSERT_NE(l2_, nullptr);
    }

    void TearDown() override {
        cache::l2_close(&l2_);
        cache::l2_destroy(kTestDbPath);
    }

    cache::L2DiskCache* setup_l2_with_entry(apg::ApgTransformContext* ctx, policy::RoutePolicy* pol,
                                            const char* path, std::uint16_t status_code,
                                            std::int64_t expires_at) {

        // build key
        cache::CacheKeyInput ki{};
        ki.method = policy::HttpMethod::Get;
        ki.route_id = pol->route_id;
        ki.path = path;
        ki.policy_version = pol->route_id;
        char key_buf[cache::kCacheKeyMaxLen] = {};
        cache::build_cache_key(ki, key_buf, sizeof(key_buf));

        // store entry
        cache::CacheEntry e{};
        std::strncpy(e.key, key_buf, cache::kCacheKeyMaxLen - 1);
        e.status_code = status_code;
        e.expires_at_epoch_ms = expires_at;
        const char body[] = "cached";
        e.body = body;
        e.body_len = 6;
        cache::l2_put(l2_, e);

        // wire context
        ctx->matched_policy = pol;
        ctx->l2_cache = l2_;
        std::strncpy(ctx->raw_path, path, sizeof(ctx->raw_path) - 1);
        return l2_;
    }

    cache::L2DiskCache* l2_ = nullptr;
};

TEST_F(L2CacheLookupStageTest, DisabledPolicySkips) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Default; // Not Store

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &pol;
    ctx.l2_cache = l2_;

    cache_key_prepare_stage(ctx);
    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_FALSE(ctx.cache_hit);
    EXPECT_STREQ(out.note, "cache-disabled");
}

TEST_F(L2CacheLookupStageTest, KeyNotReadyReturnsContinue) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &pol;
    ctx.l2_cache = l2_;

    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "key-not-ready");
    EXPECT_FALSE(ctx.cache_hit);
}

TEST_F(L2CacheLookupStageTest, L2HitPreparesResponse) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    setup_l2_with_entry(&ctx, &pol, "/api/items", 200, 9999999);

    cache_key_prepare_stage(ctx);
    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::SkipRemaining);
    EXPECT_TRUE(ctx.cache_hit);
    EXPECT_STREQ(ctx.cache_layer, "L2");
    EXPECT_TRUE(ctx.should_return_immediate_response);
    EXPECT_EQ(ctx.cached_response.status_code, 200);
    EXPECT_EQ(ctx.cached_response.body_len, 6u);
    EXPECT_EQ(std::memcmp(ctx.cached_response.body, "cached", 6), 0);
    // Verify body points into l2_body_buf
    EXPECT_EQ(ctx.cached_response.body, ctx.l2_body_buf);
}

TEST_F(L2CacheLookupStageTest, L2MissContinues) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &pol;
    ctx.l2_cache = l2_;
    std::strncpy(ctx.raw_path, "/not/in/cache", sizeof(ctx.raw_path) - 1);

    cache_key_prepare_stage(ctx);
    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_FALSE(ctx.cache_hit);
    EXPECT_STREQ(out.note, "l2-miss");
}

TEST_F(L2CacheLookupStageTest, ExpiredEntryRejected) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;

    apg::ApgTransformContext ctx{};
    setup_l2_with_entry(&ctx, &pol, "/api/items", 200, 1000); // Expired at 1000ms
    ctx.request_epoch_ms = 2000;                              // Requested at 2000ms

    cache_key_prepare_stage(ctx);
    auto out = l2_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_FALSE(ctx.cache_hit);
    EXPECT_STREQ(out.note, "l2-expired");
}

} // namespace bytetaper::stages

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "policy/route_policy.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/l1_cache_lookup_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

class L1CacheLookupStageTest : public ::testing::Test {
protected:
    void SetUp() override {
        l1_cache_ = std::make_unique<cache::L1Cache>();
        l1_init(l1_cache_.get());
        store_.count = 0;
        runtime::route_cache_epoch_register(&store_, "rt1");
    }

    std::unique_ptr<cache::L1Cache> l1_cache_;
    runtime::RouteCacheEpochStore store_;
    policy::RoutePolicy policy_{};
};

TEST_F(L1CacheLookupStageTest, DisabledPolicySkips) {
    policy_.cache.behavior = policy::CacheBehavior::Default;
    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;

    cache_key_prepare_stage(ctx);
    auto out = l1_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "cache-disabled");
    EXPECT_FALSE(ctx.cache_hit);
}

TEST_F(L1CacheLookupStageTest, KeyNotReadyReturnsContinue) {
    policy_.cache.behavior = policy::CacheBehavior::Store;
    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_.get();

    auto out = l1_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "key-not-ready");
    EXPECT_FALSE(ctx.cache_hit);
}

TEST_F(L1CacheLookupStageTest, L1MissContinues) {
    policy_.cache.behavior = policy::CacheBehavior::Store;
    policy_.route_id = "rt1";
    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_.get();
    ctx.route_cache_epoch_store = &store_;
    std::strncpy(ctx.raw_path, "/api", sizeof(ctx.raw_path) - 1);
    cache_key_prepare_stage(ctx);
    auto out = l1_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "l1-miss");
    EXPECT_FALSE(ctx.cache_hit);
}

TEST_F(L1CacheLookupStageTest, L1HitPreparesResponse) {
    policy_.cache.behavior = policy::CacheBehavior::Store;
    policy_.route_id = "rt1";
    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_.get();
    std::strncpy(ctx.raw_path, "/api", sizeof(ctx.raw_path) - 1);

    // Build the expected key
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy_.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy_.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    ASSERT_TRUE(cache::build_cache_key(ki, key_buf, sizeof(key_buf)));

    // Store entry in cache
    cache::CacheEntry entry{};
    std::strncpy(entry.key, key_buf, cache::kCacheKeyMaxLen - 1);
    entry.status_code = 200;
    cache::l1_put(l1_cache_.get(), entry);

    ctx.route_cache_epoch_store = &store_;
    cache_key_prepare_stage(ctx);
    auto out = l1_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::SkipRemaining);
    EXPECT_STREQ(out.note, "l1-hit");
    EXPECT_TRUE(ctx.cache_hit);
    EXPECT_STREQ(ctx.cache_layer, "L1");
    EXPECT_TRUE(ctx.should_return_immediate_response);
    EXPECT_EQ(ctx.cached_response.status_code, 200u);
}

TEST_F(L1CacheLookupStageTest, ExpiredHitMisses) {
    policy_.cache.behavior = policy::CacheBehavior::Store;
    policy_.route_id = "rt1";
    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_.get();
    std::strncpy(ctx.raw_path, "/api", sizeof(ctx.raw_path) - 1);
    ctx.request_epoch_ms = 2000;

    // Build the expected key
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy_.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy_.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    ASSERT_TRUE(cache::build_cache_key(ki, key_buf, sizeof(key_buf)));

    // Store expired entry in cache
    cache::CacheEntry entry{};
    std::strncpy(entry.key, key_buf, cache::kCacheKeyMaxLen - 1);
    entry.status_code = 200;
    entry.expires_at_epoch_ms = 1000; // Expired at 2000
    cache::l1_put(l1_cache_.get(), entry);

    ctx.route_cache_epoch_store = &store_;
    cache_key_prepare_stage(ctx);
    auto out = l1_cache_lookup_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "l1-miss");
    EXPECT_FALSE(ctx.cache_hit);
}

} // namespace bytetaper::stages

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "metrics/cache_metrics.h"
#include "policy/route_policy.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/l1_cache_store_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

class L1CacheStoreStageTest : public ::testing::Test {
protected:
    void SetUp() override {
        l1_cache_ptr_ = std::make_unique<cache::L1Cache>();
        l1_init(l1_cache_ptr_.get());
        store_.count = 0;
        runtime::route_cache_epoch_register(&store_, "rt1");
    }

    std::unique_ptr<cache::L1Cache> l1_cache_ptr_;
    runtime::RouteCacheEpochStore store_;
    policy::RoutePolicy policy_{};
    metrics::CacheMetrics cache_metrics_{};
};

TEST_F(L1CacheStoreStageTest, EligibleResponseStored) {
    policy_.route_id = "rt1";
    policy_.cache.behavior = policy::CacheBehavior::Store;
    policy_.cache.ttl_seconds = 60;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_ptr_.get();
    ctx.cache_metrics = &cache_metrics_;
    ctx.route_cache_epoch_store = &store_;
    ctx.request_method = policy::HttpMethod::Get;
    ctx.request_epoch_ms = 1000;
    std::strncpy(ctx.raw_path, "/api/items", sizeof(ctx.raw_path) - 1);
    ctx.response_status_code = 200;
    const char body[] = "hello";
    ctx.response_body = body;
    ctx.response_body_len = 5;
    std::strncpy(ctx.response_content_type, "application/json", cache::kCacheContentTypeMaxLen - 1);

    cache_key_prepare_stage(ctx);
    auto out = l1_cache_store_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "stored");

    EXPECT_EQ(cache_metrics_.l1_store_attempt.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_success.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_skipped.load(), 0u);

    // Verify retrievable
    char key_buf[cache::kCacheKeyMaxLen] = {};
    cache::CacheKeyInput ki{};
    ki.method = policy::HttpMethod::Get;
    ki.route_id = "rt1";
    ki.path = "/api/items";
    ki.policy_version = "rt1";
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    ASSERT_TRUE(cache::build_cache_key(ki, key_buf, sizeof(key_buf)));

    cache::CacheEntry hit{};
    char body_buf[cache::kL1MaxBodySize];
    EXPECT_TRUE(
        cache::l1_get(l1_cache_ptr_.get(), key_buf, 1000, &hit, body_buf, sizeof(body_buf)));
    EXPECT_EQ(hit.status_code, 200);
    EXPECT_EQ(hit.body_len, 5u);
    EXPECT_EQ(hit.expires_at_epoch_ms, 1000 + 60 * 1000);
}

TEST_F(L1CacheStoreStageTest, NonGetNotStored) {
    policy_.route_id = "rt1";
    policy_.cache.behavior = policy::CacheBehavior::Store;
    policy_.cache.ttl_seconds = 60;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_ptr_.get();
    ctx.cache_metrics = &cache_metrics_;
    ctx.route_cache_epoch_store = &store_;
    ctx.request_method = policy::HttpMethod::Post; // Non-GET
    ctx.response_status_code = 200;
    ctx.response_body = "body";
    ctx.response_body_len = 4;

    cache_key_prepare_stage(ctx);
    auto out = l1_cache_store_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "non-get");

    EXPECT_EQ(cache_metrics_.l1_store_attempt.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_skipped.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_success.load(), 0u);

    for (std::size_t s = 0; s < cache::kL1ShardCount; ++s) {
        for (std::size_t i = 0; i < cache::kL1SlotsPerShard; ++i) {
            EXPECT_EQ(l1_cache_ptr_->shards[s].generations[i], 0u);
        }
    }
}

TEST_F(L1CacheStoreStageTest, Non2xxNotStored) {
    policy_.route_id = "rt1";
    policy_.cache.behavior = policy::CacheBehavior::Store;
    policy_.cache.ttl_seconds = 60;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_ptr_.get();
    ctx.cache_metrics = &cache_metrics_;
    ctx.route_cache_epoch_store = &store_;
    ctx.request_method = policy::HttpMethod::Get;
    ctx.response_status_code = 404; // Non-2xx
    ctx.response_body = "body";
    ctx.response_body_len = 4;

    cache_key_prepare_stage(ctx);
    auto out = l1_cache_store_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "non-2xx");

    EXPECT_EQ(cache_metrics_.l1_store_attempt.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_skipped.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_success.load(), 0u);

    for (std::size_t s = 0; s < cache::kL1ShardCount; ++s) {
        for (std::size_t i = 0; i < cache::kL1SlotsPerShard; ++i) {
            EXPECT_EQ(l1_cache_ptr_->shards[s].generations[i], 0u);
        }
    }
}

TEST_F(L1CacheStoreStageTest, MissingKeyNotStored) {
    policy_.route_id = nullptr; // Fails key build
    policy_.cache.behavior = policy::CacheBehavior::Store;
    policy_.cache.ttl_seconds = 60;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy_;
    ctx.l1_cache = l1_cache_ptr_.get();
    ctx.cache_metrics = &cache_metrics_;
    ctx.route_cache_epoch_store = &store_;
    ctx.request_method = policy::HttpMethod::Get;
    ctx.response_status_code = 200;
    ctx.response_body = "body";
    ctx.response_body_len = 4;

    cache_key_prepare_stage(ctx);
    auto out = l1_cache_store_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "key-not-ready");

    EXPECT_EQ(cache_metrics_.l1_store_attempt.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_skipped.load(), 1u);
    EXPECT_EQ(cache_metrics_.l1_store_success.load(), 0u);

    for (std::size_t s = 0; s < cache::kL1ShardCount; ++s) {
        for (std::size_t i = 0; i < cache::kL1SlotsPerShard; ++i) {
            EXPECT_EQ(l1_cache_ptr_->shards[s].generations[i], 0u);
        }
    }
}

} // namespace bytetaper::stages

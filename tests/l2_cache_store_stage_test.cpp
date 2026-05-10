// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry_codec.h"
#include "cache/cache_key.h"
#include "cache/l2_disk_cache.h"
#include "stages/l2_cache_store_stage.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::stages {

static const char* kTestDbPath = "/tmp/bytetaper_l2_store_stage_test";

class L2CacheStoreStageTest : public ::testing::Test {
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

    cache::L2DiskCache* l2_ = nullptr;
};

TEST_F(L2CacheStoreStageTest, CodecDecodesEntry) {
    cache::CacheEntry e{};
    std::strncpy(e.key, "test-key", sizeof(e.key) - 1);
    e.status_code = 201;
    std::strncpy(e.content_type, "text/plain", sizeof(e.content_type) - 1);
    const char body[] = "hello codec";
    e.body = body;
    e.body_len = sizeof(body);
    e.created_at_epoch_ms = 1000;
    e.expires_at_epoch_ms = 2000;

    char buf[2048];
    std::size_t written = cache::cache_entry_encode(e, buf, sizeof(buf));
    ASSERT_GT(written, 0u);

    cache::CacheEntry out{};
    char body_buf[1024];
    bool ok = cache::cache_entry_decode(buf, written, &out, body_buf, sizeof(body_buf));
    ASSERT_TRUE(ok);

    EXPECT_STREQ(out.key, "test-key");
    EXPECT_EQ(out.status_code, 201);
    EXPECT_STREQ(out.content_type, "text/plain");
    EXPECT_EQ(out.body_len, sizeof(body));
    EXPECT_EQ(std::memcmp(out.body, body, out.body_len), 0);
    EXPECT_EQ(out.created_at_epoch_ms, 1000);
    EXPECT_EQ(out.expires_at_epoch_ms, 2000);
}

TEST_F(L2CacheStoreStageTest, EligibleResponseStored) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt1";
    pol.cache.behavior = policy::CacheBehavior::Store;
    pol.cache.ttl_seconds = 60;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &pol;
    ctx.l2_cache = l2_;
    ctx.request_method = policy::HttpMethod::Get;
    ctx.response_status_code = 200;
    const char body[] = "upstream content";
    ctx.response_body = body;
    ctx.response_body_len = sizeof(body);
    std::strncpy(ctx.response_content_type, "application/json",
                 sizeof(ctx.response_content_type) - 1);
    std::strncpy(ctx.raw_path, "/api/v1/resource", sizeof(ctx.raw_path) - 1);
    ctx.request_epoch_ms = 5000;

    auto out_stage = l2_cache_store_stage(ctx);
    EXPECT_EQ(out_stage.result, apg::StageResult::Continue);
    EXPECT_STREQ(out_stage.note, "stored");

    // Verify retrieval
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = pol.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = pol.route_id;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    cache::build_cache_key(ki, key_buf, sizeof(key_buf));

    cache::CacheEntry retrieved{};
    char res_body_buf[1024];
    EXPECT_TRUE(cache::l2_get(l2_, key_buf, 6000, &retrieved, res_body_buf, sizeof(res_body_buf)));
    EXPECT_EQ(retrieved.status_code, 200);
    EXPECT_EQ(retrieved.body_len, sizeof(body));
    EXPECT_EQ(std::memcmp(retrieved.body, body, retrieved.body_len), 0);
    EXPECT_EQ(retrieved.expires_at_epoch_ms, 5000 + 60 * 1000);
}

TEST_F(L2CacheStoreStageTest, RestartSimulationReadsFromSameDbPath) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt2";
    pol.cache.behavior = policy::CacheBehavior::Store;
    pol.cache.ttl_seconds = 300;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &pol;
    ctx.l2_cache = l2_;
    ctx.request_method = policy::HttpMethod::Get;
    ctx.response_status_code = 200;
    ctx.response_body = "persistent";
    ctx.response_body_len = 10;
    std::strncpy(ctx.raw_path, "/persist", sizeof(ctx.raw_path) - 1);
    ctx.request_epoch_ms = 10000;

    l2_cache_store_stage(ctx);

    // Close and Reopen
    cache::l2_close(&l2_);
    l2_ = cache::l2_open(kTestDbPath);
    ASSERT_NE(l2_, nullptr);

    // Re-verify retrieval
    cache::CacheKeyInput ki{};
    ki.method = policy::HttpMethod::Get;
    ki.route_id = pol.route_id;
    ki.path = "/persist";
    ki.policy_version = pol.route_id;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    cache::build_cache_key(ki, key_buf, sizeof(key_buf));

    cache::CacheEntry retrieved{};
    char res_body_buf[1024];
    EXPECT_TRUE(cache::l2_get(l2_, key_buf, 11000, &retrieved, res_body_buf, sizeof(res_body_buf)));
    EXPECT_EQ(retrieved.status_code, 200);
    EXPECT_EQ(retrieved.body_len, 10u);
    EXPECT_EQ(std::memcmp(retrieved.body, "persistent", 10), 0);
}

TEST_F(L2CacheStoreStageTest, NonCacheableNotStored) {
    policy::RoutePolicy pol{};
    pol.route_id = "rt3";
    pol.cache.behavior = policy::CacheBehavior::Store;
    pol.cache.ttl_seconds = 60;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &pol;
    ctx.l2_cache = l2_;
    ctx.request_method = policy::HttpMethod::Post; // Non-GET
    ctx.response_status_code = 200;
    ctx.response_body = "data";
    ctx.response_body_len = 4;
    std::strncpy(ctx.raw_path, "/api", sizeof(ctx.raw_path) - 1);

    auto out = l2_cache_store_stage(ctx);
    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "non-get");

    // Verify not in cache
    cache::CacheKeyInput ki{};
    ki.method = policy::HttpMethod::Get; // Build key as if it was GET to check existence
    ki.route_id = pol.route_id;
    ki.path = "/api";
    ki.policy_version = pol.route_id;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    cache::build_cache_key(ki, key_buf, sizeof(key_buf));

    cache::CacheEntry retrieved{};
    char res_body_buf[1024];
    EXPECT_FALSE(cache::l2_get(l2_, key_buf, 0, &retrieved, res_body_buf, sizeof(res_body_buf)));
}

TEST_F(L2CacheStoreStageTest, L2GetResultClassification) {
    const char* key = "test-result-key";

    // 1. Missing Key -> Miss
    cache::CacheEntry retrieved{};
    char res_body_buf[1024];
    cache::L2GetResult r =
        cache::l2_get_result(l2_, key, 1000, &retrieved, res_body_buf, sizeof(res_body_buf));
    EXPECT_EQ(r, cache::L2GetResult::Miss);

    // Store a valid entry
    cache::CacheEntry entry{};
    std::strncpy(entry.key, key, sizeof(entry.key) - 1);
    entry.status_code = 200;
    std::strncpy(entry.content_type, "text/plain", sizeof(entry.content_type) - 1);
    const char body[] = "test-body-content";
    entry.body = body;
    entry.body_len = sizeof(body);
    entry.created_at_epoch_ms = 1000;
    entry.expires_at_epoch_ms = 2000;
    ASSERT_TRUE(cache::l2_put(l2_, entry));

    // 2. Body fits buffer -> Hit
    r = cache::l2_get_result(l2_, key, 1500, &retrieved, res_body_buf, sizeof(res_body_buf));
    EXPECT_EQ(r, cache::L2GetResult::Hit);
    EXPECT_EQ(retrieved.body_len, sizeof(body));
    EXPECT_EQ(std::memcmp(retrieved.body, body, retrieved.body_len), 0);

    // 3. Expired entry -> Expired
    r = cache::l2_get_result(l2_, key, 2500, &retrieved, res_body_buf, sizeof(res_body_buf));
    EXPECT_EQ(r, cache::L2GetResult::Expired);

    // 4. Body larger than buffer -> BodyTooLargeForBuffer
    r = cache::l2_get_result(l2_, key, 1500, &retrieved, res_body_buf, 5);
    EXPECT_EQ(r, cache::L2GetResult::BodyTooLargeForBuffer);
}

} // namespace bytetaper::stages

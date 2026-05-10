// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry_codec.h"
#include "cache/l2_disk_cache.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::cache {

static const char* kTestDbPath = "/tmp/bytetaper_l2_test";

class L2RocksDbCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        l2_destroy(kTestDbPath);
        cache_ = l2_open(kTestDbPath);
        ASSERT_NE(cache_, nullptr);
    }

    void TearDown() override {
        l2_close(&cache_);
        l2_destroy(kTestDbPath);
    }

    L2DiskCache* cache_ = nullptr;
};

TEST_F(L2RocksDbCacheTest, PutGetReturnsSameEntry) {
    CacheEntry e{};
    std::strncpy(e.key, "GET|rt1|/api/items||v1", kCacheKeyMaxLen - 1);
    e.status_code = 200;
    std::strncpy(e.content_type, "application/json", kCacheContentTypeMaxLen - 1);
    const char body[] = "{\"id\": 1, \"name\": \"item1\"}";
    e.body = body;
    e.body_len = std::strlen(body);
    e.created_at_epoch_ms = 1000;
    e.expires_at_epoch_ms = 9999999;

    EXPECT_TRUE(l2_put(cache_, e));

    CacheEntry out{};
    char body_buf[128] = {};
    EXPECT_TRUE(l2_get(cache_, e.key, 0, &out, body_buf, sizeof(body_buf)));

    EXPECT_EQ(out.status_code, e.status_code);
    EXPECT_STREQ(out.content_type, e.content_type);
    EXPECT_EQ(out.body_len, e.body_len);
    EXPECT_EQ(std::memcmp(out.body, e.body, e.body_len), 0);
    EXPECT_EQ(out.created_at_epoch_ms, e.created_at_epoch_ms);
    EXPECT_EQ(out.expires_at_epoch_ms, e.expires_at_epoch_ms);
}

TEST_F(L2RocksDbCacheTest, UnknownKeyMisses) {
    CacheEntry out{};
    char body_buf[64] = {};
    EXPECT_FALSE(l2_get(cache_, "non_existent_key", 0, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L2RocksDbCacheTest, RemoveDeletesEntry) {
    CacheEntry e{};
    std::strncpy(e.key, "test_key", kCacheKeyMaxLen - 1);
    e.body = "data";
    e.body_len = 4;
    e.expires_at_epoch_ms = 9999999;

    EXPECT_TRUE(l2_put(cache_, e));
    EXPECT_TRUE(l2_remove(cache_, e.key));

    CacheEntry out{};
    char body_buf[64] = {};
    EXPECT_FALSE(l2_get(cache_, e.key, 0, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L2RocksDbCacheTest, ExpiredEntryMisses) {
    CacheEntry e{};
    std::strncpy(e.key, "expired_key", kCacheKeyMaxLen - 1);
    e.body = "old_data";
    e.body_len = 8;
    e.expires_at_epoch_ms = 1000; // Expired at 1000ms

    EXPECT_TRUE(l2_put(cache_, e));

    CacheEntry out{};
    char body_buf[64] = {};
    // current time is 2000ms, entry expired at 1000ms
    EXPECT_FALSE(l2_get(cache_, e.key, 2000, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L2RocksDbCacheTest, BufferTooSmallReturnsFalse) {
    CacheEntry e{};
    std::strncpy(e.key, "large_key", kCacheKeyMaxLen - 1);
    const char body[] = "this is a long body";
    e.body = body;
    e.body_len = std::strlen(body);
    e.expires_at_epoch_ms = 9999999;

    EXPECT_TRUE(l2_put(cache_, e));

    CacheEntry out{};
    char body_buf[5] = {}; // way too small
    EXPECT_FALSE(l2_get(cache_, e.key, 0, &out, body_buf, sizeof(body_buf)));
}

TEST(L2RocksDbCacheOptionsTest, DefaultOptionsRoundtrip) {
    const char* path = "/tmp/bytetaper_l2_opts_default";
    l2_destroy(path);
    L2DiskCache* cache = l2_open(path);
    ASSERT_NE(cache, nullptr);

    CacheEntry e{};
    std::strncpy(e.key, "key_def", kCacheKeyMaxLen - 1);
    e.body = "default_opt_data";
    e.body_len = 16;
    e.expires_at_epoch_ms = 9999999;

    EXPECT_TRUE(l2_put(cache, e));

    CacheEntry out{};
    char body_buf[64] = {};
    EXPECT_TRUE(l2_get(cache, e.key, 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_EQ(out.body_len, e.body_len);
    EXPECT_STREQ(out.body, e.body);

    l2_close(&cache);
    l2_destroy(path);
}

TEST(L2RocksDbCacheOptionsTest, CustomOptionsRoundtrip) {
    const char* path = "/tmp/bytetaper_l2_opts_custom";
    l2_destroy(path);

    L2CacheOptions opts{};
    opts.block_cache_mb = 8;
    opts.write_buffer_mb = 4;
    opts.max_background_jobs = 1;
    opts.enable_compression = false;

    L2DiskCache* cache = l2_open_with_options(path, opts);
    ASSERT_NE(cache, nullptr);

    CacheEntry e{};
    std::strncpy(e.key, "key_cust", kCacheKeyMaxLen - 1);
    e.body = "custom_opt_data";
    e.body_len = 15;
    e.expires_at_epoch_ms = 9999999;

    EXPECT_TRUE(l2_put(cache, e));

    CacheEntry out{};
    char body_buf[64] = {};
    EXPECT_TRUE(l2_get(cache, e.key, 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_EQ(out.body_len, e.body_len);
    EXPECT_STREQ(out.body, e.body);

    l2_close(&cache);
    l2_destroy(path);
}

TEST(L2RocksDbCacheOptionsTest, WalDisabledRoundtrip) {
    const char* path = "/tmp/bytetaper_l2_opts_wal";
    l2_destroy(path);

    L2CacheOptions opts{};
    opts.disable_wal = true;

    L2DiskCache* cache = l2_open_with_options(path, opts);
    ASSERT_NE(cache, nullptr);

    CacheEntry e{};
    std::strncpy(e.key, "key_wal", kCacheKeyMaxLen - 1);
    e.body = "wal_disabled_data";
    e.body_len = 17;
    e.expires_at_epoch_ms = 9999999;

    EXPECT_TRUE(l2_put(cache, e));

    CacheEntry out{};
    char body_buf[64] = {};
    EXPECT_TRUE(l2_get(cache, e.key, 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_EQ(out.body_len, e.body_len);
    EXPECT_STREQ(out.body, e.body);

    l2_close(&cache);
    l2_destroy(path);
}

TEST(L2RocksDbCacheOptionsTest, TtlBehaviorUnchanged) {
    const char* path = "/tmp/bytetaper_l2_opts_ttl";
    l2_destroy(path);

    L2DiskCache* cache = l2_open(path);
    ASSERT_NE(cache, nullptr);

    CacheEntry e{};
    std::strncpy(e.key, "key_ttl", kCacheKeyMaxLen - 1);
    e.body = "ttl_data";
    e.body_len = 8;
    std::int64_t now = 5000;
    e.expires_at_epoch_ms = now - 1; // Expired

    EXPECT_TRUE(l2_put(cache, e));

    CacheEntry out{};
    char body_buf[64] = {};
    EXPECT_FALSE(l2_get(cache, e.key, now, &out, body_buf, sizeof(body_buf)));

    l2_close(&cache);
    l2_destroy(path);
}

TEST_F(L2RocksDbCacheTest, PutResultSuccessAndOversized) {
    CacheEntry e{};
    std::strncpy(e.key, "put_res_key", kCacheKeyMaxLen - 1);
    e.status_code = 200;
    std::strncpy(e.content_type, "text/plain", kCacheContentTypeMaxLen - 1);
    const char body[] = "test_put_result";
    e.body = body;
    e.body_len = std::strlen(body);
    e.created_at_epoch_ms = 1000;
    e.expires_at_epoch_ms = 9999999;

    char enc_buf[2048] = {};
    L2PutResult pr = l2_put_result(cache_, e, enc_buf, sizeof(enc_buf));
    EXPECT_EQ(pr, L2PutResult::Stored);

    CacheEntry out{};
    char body_buf[128] = {};
    L2GetResult gr = l2_get_result(cache_, e.key, 0, &out, body_buf, sizeof(body_buf));
    EXPECT_EQ(gr, L2GetResult::Hit);

    // Buffer too small for encoding
    char tiny_enc_buf[10] = {};
    L2PutResult pr_tiny = l2_put_result(cache_, e, tiny_enc_buf, sizeof(tiny_enc_buf));
    EXPECT_EQ(pr_tiny, L2PutResult::BodyTooLarge);

    // Body exceeds limit
    CacheEntry massive_entry = e;
    massive_entry.body_len = kL2MaxBodySize + 10;
    std::vector<char> large_enc_buf(kCacheEntryEncodedOverhead + massive_entry.body_len);
    L2PutResult pr_massive =
        l2_put_result(cache_, massive_entry, large_enc_buf.data(), large_enc_buf.size());
    EXPECT_EQ(pr_massive, L2PutResult::BodyTooLarge);

    // Null cache or buffer
    L2PutResult pr_null = l2_put_result(nullptr, e, enc_buf, sizeof(enc_buf));
    EXPECT_EQ(pr_null, L2PutResult::EncodeError);
}

} // namespace bytetaper::cache

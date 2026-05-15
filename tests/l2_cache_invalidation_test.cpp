// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l2_disk_cache.h"

#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

using namespace bytetaper::cache;

class L2CacheInvalidationTest : public ::testing::Test {
protected:
    std::string db_path = "test_l2_invalidate_db";
    L2DiskCache* cache = nullptr;

    void SetUp() override {
        std::filesystem::remove_all(db_path);
        cache = l2_open(db_path.c_str());
        ASSERT_NE(cache, nullptr);
    }

    void TearDown() override {
        if (cache) {
            l2_close(&cache);
        }
        std::filesystem::remove_all(db_path);
    }
};

TEST_F(L2CacheInvalidationTest, RemoveExistingKeyMakesGetMiss) {
    CacheEntry entry;
    std::strncpy(entry.key, "k1", sizeof(entry.key) - 1);
    entry.body = "v1";
    entry.body_len = 2;
    entry.expires_at_epoch_ms = 11000;
    entry.created_at_epoch_ms = 1000;

    char enc_buf[2048];
    ASSERT_EQ(l2_put_result(cache, entry, enc_buf, sizeof(enc_buf)), L2PutResult::Stored);

    CacheEntry hit;
    char scratch[2048];
    EXPECT_EQ(l2_get_result(cache, "k1", 1500, &hit, scratch, sizeof(scratch)), L2GetResult::Hit);

    EXPECT_TRUE(l2_remove(cache, "k1"));
    EXPECT_EQ(l2_get_result(cache, "k1", 1500, &hit, scratch, sizeof(scratch)), L2GetResult::Miss);
}

TEST_F(L2CacheInvalidationTest, RemoveMissingKeyReturnsFalseOrMiss) {
    EXPECT_FALSE(l2_remove(cache, "missing"));
}

TEST_F(L2CacheInvalidationTest, RemoveNullCacheFailsSafely) {
    EXPECT_FALSE(l2_remove(nullptr, "key"));
}

TEST_F(L2CacheInvalidationTest, RemoveNullKeyFailsSafely) {
    EXPECT_FALSE(l2_remove(cache, nullptr));
}

TEST_F(L2CacheInvalidationTest, RemoveDoesNotAffectOtherKeys) {
    char enc_buf[2048];
    char scratch[2048];
    CacheEntry hit;

    CacheEntry e1;
    std::strncpy(e1.key, "k1", sizeof(e1.key) - 1);
    e1.body = "v1";
    e1.body_len = 2;
    e1.expires_at_epoch_ms = 11000;
    e1.created_at_epoch_ms = 1000;
    l2_put_result(cache, e1, enc_buf, sizeof(enc_buf));

    CacheEntry e2;
    std::strncpy(e2.key, "k2", sizeof(e2.key) - 1);
    e2.body = "v2";
    e2.body_len = 2;
    e2.expires_at_epoch_ms = 11000;
    e2.created_at_epoch_ms = 1000;
    l2_put_result(cache, e2, enc_buf, sizeof(enc_buf));

    EXPECT_TRUE(l2_remove(cache, "k1"));
    EXPECT_EQ(l2_get_result(cache, "k1", 1500, &hit, scratch, sizeof(scratch)), L2GetResult::Miss);
    EXPECT_EQ(l2_get_result(cache, "k2", 1500, &hit, scratch, sizeof(scratch)), L2GetResult::Hit);
}

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/l1_cache.h"
#include "hash/hash.h"

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace bytetaper::cache {

class L1CacheInvalidationTest : public ::testing::Test {
protected:
    L1Cache cache;

    void SetUp() override {
        l1_init(&cache);
    }

    CacheEntry make_entry(const char* key, const char* body) {
        CacheEntry e{};
        std::strncpy(e.key, key, kCacheKeyMaxLen - 1);
        e.body = body;
        e.body_len = std::strlen(body);
        e.created_at_epoch_ms = 1000;
        e.expires_at_epoch_ms = 2000;
        return e;
    }

    std::vector<std::string> keys_for_shard(std::size_t shard_idx, std::size_t count) {
        std::vector<std::string> results;
        std::uint64_t i = 0;
        while (results.size() < count) {
            std::string key = "test_key_" + std::to_string(i++);
            if (bytetaper::hash::hash_cstr_runtime(key.c_str()) % kL1ShardCount == shard_idx) {
                results.push_back(key);
            }
        }
        return results;
    }
};

TEST_F(L1CacheInvalidationTest, RemoveExistingKeyMakesLookupMiss) {
    auto entry = make_entry("user:123", "{\"id\":123}");
    l1_put(&cache, entry);

    // Verify it exists first
    CacheEntry out{};
    char body_out[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(&cache, "user:123", 1500, &out, body_out, sizeof(body_out)));

    // Act
    EXPECT_EQ(l1_remove(&cache, "user:123"), L1RemoveResult::Removed);

    // Assert
    EXPECT_FALSE(l1_get(&cache, "user:123", 1500, &out, body_out, sizeof(body_out)));
}

TEST_F(L1CacheInvalidationTest, RemoveMissingKeyReturnsMiss) {
    EXPECT_EQ(l1_remove(&cache, "nonexistent"), L1RemoveResult::Miss);
}

TEST_F(L1CacheInvalidationTest, RemoveNullCacheReturnsInvalidArgument) {
    EXPECT_EQ(l1_remove(nullptr, "key"), L1RemoveResult::InvalidArgument);
}

TEST_F(L1CacheInvalidationTest, RemoveNullKeyReturnsInvalidArgument) {
    EXPECT_EQ(l1_remove(&cache, nullptr), L1RemoveResult::InvalidArgument);
}

TEST_F(L1CacheInvalidationTest, RemoveOnlyAffectsMatchingKey) {
    // Use same shard to prove exact key removal within a single locked shard
    auto shard_keys = keys_for_shard(42, 2);
    const char* keyA = shard_keys[0].c_str();
    const char* keyB = shard_keys[1].c_str();

    auto entryA = make_entry(keyA, "bodyA");
    auto entryB = make_entry(keyB, "bodyB");
    l1_put(&cache, entryA);
    l1_put(&cache, entryB);

    EXPECT_EQ(l1_remove(&cache, keyA), L1RemoveResult::Removed);

    CacheEntry out{};
    char body_out[kL1MaxBodySize];
    EXPECT_FALSE(l1_get(&cache, keyA, 1500, &out, body_out, sizeof(body_out)));
    EXPECT_TRUE(l1_get(&cache, keyB, 1500, &out, body_out, sizeof(body_out)));
    EXPECT_STREQ(body_out, "bodyB");
}

TEST_F(L1CacheInvalidationTest, ReinsertAfterRemoveCanBeLookedUp) {
    auto entryOld = make_entry("key", "old");
    l1_put(&cache, entryOld);
    l1_remove(&cache, "key");

    auto entryNew = make_entry("key", "new");
    l1_put(&cache, entryNew);

    CacheEntry out{};
    char body_out[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(&cache, "key", 1500, &out, body_out, sizeof(body_out)));
    EXPECT_STREQ(body_out, "new");
}

TEST_F(L1CacheInvalidationTest, RemoveExpiredEntrySucceeds) {
    auto entry = make_entry("key", "body");
    entry.expires_at_epoch_ms = 500;
    l1_put(&cache, entry);

    // Verify it is already an expired miss at T=1500
    CacheEntry out{};
    char body_out[kL1MaxBodySize];
    EXPECT_FALSE(l1_get(&cache, "key", 1500, &out, body_out, sizeof(body_out)));

    // It should be removed even if l1_get considers it a miss
    EXPECT_EQ(l1_remove(&cache, "key"), L1RemoveResult::Removed);
}

TEST_F(L1CacheInvalidationTest, RemoveDoesNotBreakSubsequentRingWrites) {
    const std::size_t shard_idx = 7;
    auto shard_keys = keys_for_shard(shard_idx, kL1SlotsPerShard + 1);

    // 1. Fill the shard
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        l1_put(&cache, make_entry(shard_keys[i].c_str(), "val"));
    }

    auto& shard = cache.shards[shard_idx];
    std::size_t cursor_before = shard.write_cursor;
    EXPECT_EQ(cursor_before, kL1SlotsPerShard);

    // 2. Remove one key (physically middle or start, doesn't matter for ring)
    EXPECT_EQ(l1_remove(&cache, shard_keys[0].c_str()), L1RemoveResult::Removed);

    // 3. Assert write_cursor is NOT changed by removal
    EXPECT_EQ(shard.write_cursor, cursor_before);

    // 4. Put extra key - should overwrite using the NEXT ring position (which is 0 again)
    l1_put(&cache, make_entry(shard_keys[kL1SlotsPerShard].c_str(), "new_val"));
    EXPECT_EQ(shard.write_cursor, cursor_before + 1);

    // 5. Verify the extra key is readable and the removed key is still a miss
    CacheEntry out{};
    char body_out[kL1MaxBodySize];
    EXPECT_TRUE(l1_get(&cache, shard_keys[kL1SlotsPerShard].c_str(), 1500, &out, body_out,
                       sizeof(body_out)));
    EXPECT_STREQ(body_out, "new_val");
    EXPECT_FALSE(l1_get(&cache, shard_keys[0].c_str(), 1500, &out, body_out, sizeof(body_out)));
}

TEST_F(L1CacheInvalidationTest, ConvenienceWrapperReturnsTrueOnlyOnRemoved) {
    auto entry = make_entry("key", "body");
    l1_put(&cache, entry);

    EXPECT_TRUE(l1_remove_key(&cache, "key"));
    EXPECT_FALSE(l1_remove_key(&cache, "key")); // Second time is a miss
}

} // namespace bytetaper::cache

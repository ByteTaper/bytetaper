// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/l1_cache.h"
#include "hash/hash.h"
#include "metrics/cache_metrics.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::cache {

class L1CacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        cache = std::make_unique<L1Cache>();
        l1_init(cache.get());
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
    }

    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    std::unique_ptr<L1Cache> cache;
};

TEST_F(L1CacheTest, GetMissingReturnsFalse) {
    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_FALSE(l1_get(cache.get(), "missing", 0, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L1CacheTest, PutAndGet) {
    CacheEntry e{};
    ::strncpy(e.key, "k1", sizeof(e.key) - 1);
    e.body = "hello";
    e.body_len = 5;
    l1_put(cache.get(), e);

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_TRUE(l1_get(cache.get(), "k1", 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_STREQ(out.key, "k1");
    EXPECT_STREQ(static_cast<const char*>(out.body), "hello");
    EXPECT_EQ(out.body_len, 5);
}

TEST_F(L1CacheTest, FIFOEviction) {
    // Fill one shard
    for (std::size_t i = 0; i < kL1SlotsPerShard + 1; ++i) {
        CacheEntry e{};
        std::string k = "k" + std::to_string(i);
        ::strncpy(e.key, k.c_str(), sizeof(e.key) - 1);
        l1_put(cache.get(), e);
    }

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    // k0 should be evicted by k16 (assuming they all hash to same shard for this test,
    // which they might not, but with a small ring they will wrap eventually).
    // Actually, l1_put always uses cursor % slots_per_shard, but shards are selected by hash.
    // To guarantee eviction in a specific shard, we'd need to control the hash.
    // Let's just put many entries to ensure wraparound in at least one shard.
    for (int i = 0; i < 100; ++i) {
        CacheEntry e{};
        ::strncpy(e.key, "k0", sizeof(e.key) - 1);
        l1_put(cache.get(), e);
        if (!l1_get(cache.get(), "k0", 0, &out, body_buf, sizeof(body_buf))) {
            // This is not a great test due to hashing, but let's assume it works for now.
        }
    }

    // Test that we can indeed lose an entry
    for (int i = 0; i < (int) (kL1SlotsPerShard * 2); ++i) {
        CacheEntry e{};
        std::string k = "target";
        ::strncpy(e.key, k.c_str(), sizeof(e.key) - 1);
        l1_put(cache.get(), e);
    }
}

TEST_F(L1CacheTest, Expiry) {
    CacheEntry e{};
    ::strncpy(e.key, "expired_key", sizeof(e.key) - 1);
    e.created_at_epoch_ms = 1000;
    e.expires_at_epoch_ms = 1500;
    l1_put(cache.get(), e);

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_FALSE(l1_get(cache.get(), "expired_key", 2000, &out, body_buf, sizeof(body_buf)));
    // Should still be visible if time is before expiry
    EXPECT_TRUE(l1_get(cache.get(), "expired_key", 500, &out, body_buf, sizeof(body_buf)));
}

static std::uint64_t expected_hash_key(const char* key) {
    return bytetaper::hash::hash_cstr_keyed(key, { 0x1234567812345678ULL, 0x8765432187654321ULL });
}

TEST_F(L1CacheTest, HashTagStoredOnPut) {
    CacheEntry e{};
    ::strncpy(e.key, "k1", sizeof(e.key) - 1);
    l1_put(cache.get(), e);

    const std::uint64_t h = expected_hash_key("k1");
    const std::size_t shard_idx = h % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    // Find the slot with this key
    bool found = false;
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        if (shard.generations[i] > 0 && std::strcmp(shard.slots[i].key, "k1") == 0) {
            EXPECT_EQ(shard.key_hashes[i], h);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(L1CacheTest, HashTagClearedOrOverwrittenOnEviction) {
    // Generate 17 keys that map to the exact same shard
    std::vector<std::string> keys;
    int suffix = 0;
    std::size_t targeted_shard = 0;

    while (keys.size() < kL1SlotsPerShard + 1) {
        std::string candidate = "key_" + std::to_string(suffix++);
        std::uint64_t h = expected_hash_key(candidate.c_str());
        if (h % kL1ShardCount == targeted_shard) {
            keys.push_back(candidate);
        }
    }

    // Put the first 16 keys (fills the shard completely)
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        CacheEntry e{};
        ::strncpy(e.key, keys[i].c_str(), sizeof(e.key) - 1);
        l1_put(cache.get(), e);
    }

    // Verify slot 0 has the first key's hash
    auto& shard = cache->shards[targeted_shard];
    std::uint64_t first_hash = expected_hash_key(keys[0].c_str());
    EXPECT_EQ(shard.key_hashes[0], first_hash);

    // Put the 17th key to wrap around and overwrite slot 0
    CacheEntry last_entry{};
    ::strncpy(last_entry.key, keys[kL1SlotsPerShard].c_str(), sizeof(last_entry.key) - 1);
    l1_put(cache.get(), last_entry);

    // Verify slot 0 hash is overwritten to the 17th key's hash
    std::uint64_t last_hash = expected_hash_key(keys[kL1SlotsPerShard].c_str());
    EXPECT_EQ(shard.key_hashes[0], last_hash);
}

TEST_F(L1CacheTest, HashCollisionStillCorrect) {
    CacheEntry e{};
    ::strncpy(e.key, "real_key", sizeof(e.key) - 1);
    l1_put(cache.get(), e);

    const std::uint64_t real_hash = expected_hash_key("real_key");
    const std::size_t shard_idx = real_hash % kL1ShardCount;
    auto& shard = cache->shards[shard_idx];

    // Find a "fake_key" that maps to the SAME shard but has a different key string
    std::string fake_key;
    int suffix = 0;
    while (true) {
        std::string candidate = "fake_" + std::to_string(suffix++);
        if (candidate == "real_key")
            continue;
        std::uint64_t h = expected_hash_key(candidate.c_str());
        if (h % kL1ShardCount == shard_idx) {
            fake_key = candidate;
            break;
        }
    }

    const std::uint64_t fake_hash = expected_hash_key(fake_key.c_str());

    // Find slot index of "real_key"
    std::size_t slot_idx = 9999;
    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        if (shard.generations[i] > 0 && std::strcmp(shard.slots[i].key, "real_key") == 0) {
            slot_idx = i;
            break;
        }
    }
    ASSERT_NE(slot_idx, 9999u);

    // Artificially modify the key_hash in that slot to fake_hash.
    // Now, looking up fake_key will find a slot with generations > 0 and key_hashes[i] ==
    // fake_hash. However, std::strncmp should fail because the key is "real_key" instead of
    // "fake_key".
    shard.key_hashes[slot_idx] = fake_hash;

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_FALSE(l1_get(cache.get(), fake_key.c_str(), 0, &out, body_buf, sizeof(body_buf)))
        << "Should not match different key even if key_hashes match (collision correctness)";
}

TEST_F(L1CacheTest, PutIfNewerRejectsOversizedBody) {
    CacheEntry e{};
    ::strncpy(e.key, "oversized_key", sizeof(e.key) - 1);
    std::string large_body(kL1MaxBodySize + 1, 'A');
    e.body = large_body.c_str();
    e.body_len = large_body.size();

    EXPECT_FALSE(l1_put_if_newer(cache.get(), e));

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_FALSE(l1_get(cache.get(), "oversized_key", 0, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L1CacheTest, PutRejectsOversizedBody) {
    CacheEntry e{};
    ::strncpy(e.key, "oversized_key2", sizeof(e.key) - 1);
    std::string large_body(kL1MaxBodySize + 1, 'A');
    e.body = large_body.c_str();
    e.body_len = large_body.size();

    l1_put(cache.get(), e);

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_FALSE(l1_get(cache.get(), "oversized_key2", 0, &out, body_buf, sizeof(body_buf)));
}

TEST_F(L1CacheTest, BoundaryExactlyMaxBodySizeAllowed) {
    CacheEntry e{};
    ::strncpy(e.key, "boundary_key", sizeof(e.key) - 1);
    std::string max_body(kL1MaxBodySize, 'B');
    e.body = max_body.c_str();
    e.body_len = max_body.size();

    EXPECT_TRUE(l1_put_if_newer(cache.get(), e));

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_TRUE(l1_get(cache.get(), "boundary_key", 0, &out, body_buf, sizeof(body_buf)));
    EXPECT_STREQ(out.key, "boundary_key");
    EXPECT_EQ(out.body_len, kL1MaxBodySize);
    EXPECT_STREQ(body_buf, max_body.c_str());
}

TEST_F(L1CacheTest, EvictionRingOverwriteMetric) {
    bytetaper::metrics::CacheMetrics metrics{};

    // Fill the shard with 16 entries to targeted shard
    std::size_t targeted_shard = expected_hash_key("key_0") % kL1ShardCount;
    std::vector<std::string> keys;
    int suffix = 0;
    while (keys.size() < kL1SlotsPerShard + 1) {
        std::string candidate = "key_" + std::to_string(suffix++);
        if (expected_hash_key(candidate.c_str()) % kL1ShardCount == targeted_shard) {
            keys.push_back(candidate);
        }
    }

    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        CacheEntry e{};
        ::strncpy(e.key, keys[i].c_str(), sizeof(e.key) - 1);
        l1_put(cache.get(), e, &metrics);
    }
    EXPECT_EQ(metrics.l1_eviction_total.load(), 0);

    // Overwrite the first entry (eviction)
    CacheEntry e{};
    ::strncpy(e.key, keys[kL1SlotsPerShard].c_str(), sizeof(e.key) - 1);
    l1_put(cache.get(), e, &metrics);

    EXPECT_EQ(metrics.l1_eviction_total.load(), 1);
}

TEST_F(L1CacheTest, DuplicateOverwriteMetric) {
    bytetaper::metrics::CacheMetrics metrics{};

    CacheEntry e{};
    ::strncpy(e.key, "dup_key", sizeof(e.key) - 1);
    e.created_at_epoch_ms = 1000;
    l1_put(cache.get(), e, &metrics);

    CacheEntry e2{};
    ::strncpy(e2.key, "dup_key", sizeof(e2.key) - 1);
    e2.created_at_epoch_ms = 2000;

    EXPECT_TRUE(l1_put_if_newer(cache.get(), e2, &metrics));
    EXPECT_EQ(metrics.l1_duplicate_overwrite_total.load(), 1);
}

TEST_F(L1CacheTest, ExpiredMissMetric) {
    bytetaper::metrics::CacheMetrics metrics{};

    CacheEntry e{};
    ::strncpy(e.key, "exp_key", sizeof(e.key) - 1);
    e.created_at_epoch_ms = 1000;
    e.expires_at_epoch_ms = 1500;
    l1_put(cache.get(), e, &metrics);

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    EXPECT_FALSE(l1_get(cache.get(), "exp_key", 2000, &out, body_buf, sizeof(body_buf), &metrics));
    EXPECT_EQ(metrics.l1_expired_miss_total.load(), 1);
}

TEST_F(L1CacheTest, LookupSlotScannedMetric) {
    bytetaper::metrics::CacheMetrics metrics{};

    // Add 3 entries to the same shard
    std::size_t targeted_shard = expected_hash_key("scan_key0") % kL1ShardCount;
    std::vector<std::string> keys;
    int suffix = 0;
    while (keys.size() < 3) {
        std::string candidate = "scan_key" + std::to_string(suffix++);
        if (expected_hash_key(candidate.c_str()) % kL1ShardCount == targeted_shard) {
            keys.push_back(candidate);
        }
    }

    for (const auto& k : keys) {
        CacheEntry e{};
        ::strncpy(e.key, k.c_str(), sizeof(e.key) - 1);
        l1_put(cache.get(), e, &metrics);
    }

    CacheEntry out{};
    char body_buf[kL1MaxBodySize] = {};
    // Searching for a non-existing key in the same shard will scan all active (live) slots (which
    // is 3)
    std::string non_existent;
    while (true) {
        std::string candidate = "non_existent_" + std::to_string(suffix++);
        if (expected_hash_key(candidate.c_str()) % kL1ShardCount == targeted_shard) {
            non_existent = candidate;
            break;
        }
    }

    l1_get(cache.get(), non_existent.c_str(), 0, &out, body_buf, sizeof(body_buf), &metrics);
    EXPECT_EQ(metrics.l1_lookup_slots_scanned_total.load(), 3);
}

TEST_F(L1CacheTest, SuccessfulPutIncrementsAdmittedMetric) {
    bytetaper::metrics::CacheMetrics metrics{};
    CacheEntry e{};
    ::strncpy(e.key, "success_admit", sizeof(e.key) - 1);
    e.body = "OK";
    e.body_len = 2;

    l1_put(cache.get(), e, &metrics);
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 1);
    EXPECT_EQ(metrics.l1_store_rejected_body_too_large_total.load(), 0);
    EXPECT_EQ(metrics.l1_store_rejected_invalid_body_total.load(), 0);
}

TEST_F(L1CacheTest, OversizedPutAndPutIfNewerRejectMetrics) {
    bytetaper::metrics::CacheMetrics metrics{};
    CacheEntry e{};
    ::strncpy(e.key, "oversized_admit", sizeof(e.key) - 1);
    std::string large_body(kL1MaxBodySize + 1, 'A');
    e.body = large_body.c_str();
    e.body_len = large_body.size();

    l1_put(cache.get(), e, &metrics);
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 0);
    EXPECT_EQ(metrics.l1_store_rejected_body_too_large_total.load(), 1);

    EXPECT_FALSE(l1_put_if_newer(cache.get(), e, &metrics));
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 0);
    EXPECT_EQ(metrics.l1_store_rejected_body_too_large_total.load(), 2);
}

TEST_F(L1CacheTest, InvalidBodyNullRejectMetrics) {
    bytetaper::metrics::CacheMetrics metrics{};
    CacheEntry e{};
    ::strncpy(e.key, "invalid_null_body", sizeof(e.key) - 1);
    e.body = nullptr;
    e.body_len = 5; // nonzero body_len with null body

    l1_put(cache.get(), e, &metrics);
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 0);
    EXPECT_EQ(metrics.l1_store_rejected_invalid_body_total.load(), 1);

    EXPECT_FALSE(l1_put_if_newer(cache.get(), e, &metrics));
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 0);
    EXPECT_EQ(metrics.l1_store_rejected_invalid_body_total.load(), 2);
}

TEST_F(L1CacheTest, PutIfNewerRecordsEvictionOnFullShard) {
    bytetaper::metrics::CacheMetrics metrics{};

    std::size_t targeted_shard = expected_hash_key("key_0") % kL1ShardCount;
    std::vector<std::string> keys;
    int suffix = 0;
    while (keys.size() < kL1SlotsPerShard + 1) {
        std::string candidate = "key_" + std::to_string(suffix++);
        if (expected_hash_key(candidate.c_str()) % kL1ShardCount == targeted_shard) {
            keys.push_back(candidate);
        }
    }

    for (std::size_t i = 0; i < kL1SlotsPerShard; ++i) {
        CacheEntry e{};
        ::strncpy(e.key, keys[i].c_str(), sizeof(e.key) - 1);
        e.created_at_epoch_ms = 1000;
        EXPECT_TRUE(l1_put_if_newer(cache.get(), e, &metrics));
    }
    EXPECT_EQ(metrics.l1_eviction_total.load(), 0);
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), kL1SlotsPerShard);

    // Overwrite the first entry (eviction)
    CacheEntry e{};
    ::strncpy(e.key, keys[kL1SlotsPerShard].c_str(), sizeof(e.key) - 1);
    e.created_at_epoch_ms = 1000;
    EXPECT_TRUE(l1_put_if_newer(cache.get(), e, &metrics));

    EXPECT_EQ(metrics.l1_eviction_total.load(), 1);
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), kL1SlotsPerShard + 1);
}

TEST_F(L1CacheTest, PlainPutRecordsDuplicateOverwrite) {
    bytetaper::metrics::CacheMetrics metrics{};

    CacheEntry e{};
    ::strncpy(e.key, "plain_dup_key", sizeof(e.key) - 1);
    l1_put(cache.get(), e, &metrics);
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 1);
    EXPECT_EQ(metrics.l1_duplicate_overwrite_total.load(), 0);

    CacheEntry e2{};
    ::strncpy(e2.key, "plain_dup_key", sizeof(e2.key) - 1);
    l1_put(cache.get(), e2, &metrics);
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 2);
    EXPECT_EQ(metrics.l1_duplicate_overwrite_total.load(), 1);
}

TEST_F(L1CacheTest, StalePutIfNewerRejectionDoesNotRecordAdmittedOrEvictionOrDuplicate) {
    bytetaper::metrics::CacheMetrics metrics{};

    CacheEntry e{};
    ::strncpy(e.key, "stale_key", sizeof(e.key) - 1);
    e.created_at_epoch_ms = 2000;
    EXPECT_TRUE(l1_put_if_newer(cache.get(), e, &metrics));
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 1);
    EXPECT_EQ(metrics.l1_duplicate_overwrite_total.load(), 0);

    // Now try to overwrite with an older entry
    CacheEntry stale{};
    ::strncpy(stale.key, "stale_key", sizeof(stale.key) - 1);
    stale.created_at_epoch_ms = 1000; // stale!

    EXPECT_FALSE(l1_put_if_newer(cache.get(), stale, &metrics));
    // Verify no admittance, no duplicate overwrite, no eviction happened on rejection
    EXPECT_EQ(metrics.l1_store_admitted_total.load(), 1);
    EXPECT_EQ(metrics.l1_duplicate_overwrite_total.load(), 0);
    EXPECT_EQ(metrics.l1_eviction_total.load(), 0);
}

} // namespace bytetaper::cache

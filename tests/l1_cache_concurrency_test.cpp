// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/l1_cache.h"
#include "hash/hash.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace bytetaper::cache {

class L1CacheConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
        l1_init(&l1);
    }

    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    // Helper to generate a key that hashes to a specific shard
    std::string GenerateKeyForShard(std::size_t shard_idx, std::size_t item_idx) {
        for (int i = 0; i < 100000; ++i) {
            std::string candidate = "key_" + std::to_string(shard_idx) + "_" +
                                    std::to_string(item_idx) + "_" + std::to_string(i);
            std::uint64_t h = bytetaper::hash::hash_cstr_runtime(candidate.c_str());
            if (h % kL1ShardCount == shard_idx) {
                return candidate;
            }
        }
        return "";
    }

    L1Cache l1;
};

TEST_F(L1CacheConcurrencyTest, L1ConcurrentPutFromMultipleThreads) {
    constexpr int kThreadCount = 16;
    constexpr int kEntriesPerThread = 16; // Fits perfectly in the 16 slots of each shard
    std::vector<std::thread> threads;
    std::atomic<bool> start{ false };

    // Pre-generate keys to avoid latency inside the concurrent region
    std::vector<std::vector<std::string>> thread_keys(kThreadCount);
    for (int i = 0; i < kThreadCount; ++i) {
        thread_keys[i].reserve(kEntriesPerThread);
        for (int j = 0; j < kEntriesPerThread; ++j) {
            thread_keys[i].push_back(GenerateKeyForShard(i, j));
        }
    }

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([this, i, &start, &thread_keys] {
            while (!start)
                std::this_thread::yield();
            for (int j = 0; j < kEntriesPerThread; ++j) {
                CacheEntry e{};
                ::strncpy(e.key, thread_keys[i][j].c_str(), sizeof(e.key) - 1);
                e.expires_at_epoch_ms = 9999999999;
                l1_put(&l1, e);
            }
        });
    }

    start = true;
    for (auto& t : threads) {
        t.join();
    }

    // Verify all entries are present (since no shard exceeded capacity, zero evictions occurred)
    for (int i = 0; i < kThreadCount; ++i) {
        for (int j = 0; j < kEntriesPerThread; ++j) {
            CacheEntry hit{};
            char body_buf[kL1MaxBodySize];
            bool found =
                l1_get(&l1, thread_keys[i][j].c_str(), 0, &hit, body_buf, sizeof(body_buf));
            EXPECT_TRUE(found) << "Key not found: " << thread_keys[i][j];
            EXPECT_STREQ(hit.key, thread_keys[i][j].c_str());
        }
    }
}

TEST_F(L1CacheConcurrencyTest, L1ConcurrentGetPutSameKey) {
    constexpr int kWriterCount = 8;
    constexpr int kReaderCount = 8;
    constexpr int kIterations = 1000;
    const char* kSharedKey = "shared_key";

    std::vector<std::thread> threads;
    std::atomic<bool> start{ false };

    for (int i = 0; i < kWriterCount; ++i) {
        threads.emplace_back([this, &start, kSharedKey] {
            while (!start)
                std::this_thread::yield();
            for (int j = 0; j < kIterations; ++j) {
                CacheEntry e{};
                ::strncpy(e.key, kSharedKey, sizeof(e.key) - 1);
                e.expires_at_epoch_ms = 9999999999;

                char local_body[1024];
                std::memset(local_body, (char) (j % 256), sizeof(local_body));
                e.body = local_body;
                e.body_len = sizeof(local_body);
                l1_put(&l1, e);
            }
        });
    }

    for (int i = 0; i < kReaderCount; ++i) {
        threads.emplace_back([this, &start, kSharedKey] {
            while (!start)
                std::this_thread::yield();
            for (int j = 0; j < kIterations; ++j) {
                CacheEntry hit{};
                char body_buf[kL1MaxBodySize];
                bool found = l1_get(&l1, kSharedKey, 0, &hit, body_buf, sizeof(body_buf));
                if (found) {
                    // Check if the body is consistent (all bytes should be the same)
                    char expected = hit.body[0];
                    for (std::size_t k = 1; k < hit.body_len; ++k) {
                        ASSERT_EQ(hit.body[k], expected) << "Torn read detected!";
                    }
                }
            }
        });
    }

    start = true;
    for (auto& t : threads) {
        t.join();
    }
}

TEST_F(L1CacheConcurrencyTest, L1ConcurrentPutAndExpiry) {
    constexpr int kThreadCount = 8;
    constexpr int kIterations = 100;
    std::vector<std::thread> threads;
    std::atomic<bool> start{ false };
    std::atomic<int64_t> global_now_ms{ 1000 };

    // Pre-generate keys for each thread's assigned shard to guarantee zero cross-thread eviction
    std::vector<std::vector<std::string>> thread_keys(kThreadCount);
    for (int i = 0; i < kThreadCount; ++i) {
        thread_keys[i].reserve(kIterations);
        for (int j = 0; j < kIterations; ++j) {
            thread_keys[i].push_back(GenerateKeyForShard(i, j));
        }
    }

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([this, i, &start, &global_now_ms, &thread_keys] {
            while (!start)
                std::this_thread::yield();
            for (int j = 0; j < kIterations; ++j) {
                CacheEntry e{};
                ::strncpy(e.key, thread_keys[i][j].c_str(), sizeof(e.key) - 1);
                e.expires_at_epoch_ms = global_now_ms.load() + 1000;
                l1_put(&l1, e);

                CacheEntry hit{};
                char body_buf[kL1MaxBodySize];
                // Immediate get should succeed because no other thread is evicting from shard i
                EXPECT_TRUE(l1_get(&l1, thread_keys[i][j].c_str(), global_now_ms.load(), &hit,
                                   body_buf, sizeof(body_buf)));

                // Get with future time should fail
                EXPECT_FALSE(l1_get(&l1, thread_keys[i][j].c_str(), global_now_ms.load() + 2000,
                                    &hit, body_buf, sizeof(body_buf)));
            }
        });
    }

    start = true;
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace bytetaper::cache

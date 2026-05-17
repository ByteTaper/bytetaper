// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_entry.h"
#include "cache/l1_cache.h"
#include "concurrency_test_barrier.h"
#include "hash/hash.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace bytetaper::cache {

class L1CacheConcurrencyHarnessTest : public ::testing::Test {
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

TEST_F(L1CacheConcurrencyHarnessTest, ConcurrentPutGetRemoveSameKey) {
    const char* kSharedKey = "shared_concurrency_key";
    std::vector<std::thread> threads;
    StartGate gate;

    // 4 writers (2 call l1_put, 2 call l1_put_if_newer)
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([this, i, &gate, kSharedKey] {
            gate.arrive_and_wait(10);
            for (int j = 0; j < 1000; ++j) {
                CacheEntry e{};
                ::strncpy(e.key, kSharedKey, sizeof(e.key) - 1);
                e.expires_at_epoch_ms = 9999999999;
                e.created_at_epoch_ms = static_cast<std::uint64_t>(j);

                char local_body[100];
                std::memset(local_body, static_cast<char>(i), sizeof(local_body));
                e.body = local_body;
                e.body_len = sizeof(local_body);

                if (i < 2) {
                    l1_put(&l1, e);
                } else {
                    l1_put_if_newer(&l1, e);
                }
            }
        });
    }

    // 4 readers
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([this, &gate, kSharedKey] {
            gate.arrive_and_wait(10);
            for (int j = 0; j < 1000; ++j) {
                CacheEntry hit{};
                char body_buf[kL1MaxBodySize];
                bool found = l1_get(&l1, kSharedKey, 0, &hit, body_buf, sizeof(body_buf));
                if (found && hit.body_len > 0) {
                    char expected = body_buf[0];
                    for (std::size_t k = 1; k < hit.body_len; ++k) {
                        ASSERT_EQ(body_buf[k], expected) << "Torn read detected!";
                    }
                }
            }
        });
    }

    // 2 removers
    for (int i = 0; i < 2; ++i) {
        threads.emplace_back([this, &gate, kSharedKey] {
            gate.arrive_and_wait(10);
            for (int j = 0; j < 1000; ++j) {
                l1_remove_key(&l1, kSharedKey);
            }
        });
    }

    gate.release();
    for (auto& t : threads) {
        t.join();
    }
}

TEST_F(L1CacheConcurrencyHarnessTest, ShardCollisionHarness) {
    std::vector<std::string> keys;
    for (int i = 0; i < 8; ++i) {
        keys.push_back(GenerateKeyForShard(0, i));
    }

    std::vector<std::thread> threads;
    StartGate gate;

    // 8 writer threads
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([this, i, &gate, &keys] {
            gate.arrive_and_wait(16); // 8 writers + 8 readers
            for (int j = 0; j < 100; ++j) {
                CacheEntry e{};
                ::strncpy(e.key, keys[i].c_str(), sizeof(e.key) - 1);
                e.expires_at_epoch_ms = 9999999999;
                e.created_at_epoch_ms = static_cast<std::uint64_t>(j);

                char local_body[100];
                std::memset(local_body, static_cast<char>(i), sizeof(local_body));
                e.body = local_body;
                e.body_len = sizeof(local_body);
                l1_put(&l1, e);
            }
        });
    }

    // 8 reader threads doing concurrent reads on the collided key set
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([this, i, &gate, &keys] {
            gate.arrive_and_wait(16); // 8 writers + 8 readers
            for (int j = 0; j < 100; ++j) {
                int key_idx = (i + j) % 8;
                CacheEntry hit{};
                char body_buf[kL1MaxBodySize];
                bool found =
                    l1_get(&l1, keys[key_idx].c_str(), 0, &hit, body_buf, sizeof(body_buf));
                if (found && hit.body_len > 0) {
                    // Check body bounds
                    ASSERT_LE(hit.body_len, sizeof(body_buf));
                    // Verify no torn-content (all bytes match the thread-id byte that wrote it)
                    char expected = static_cast<char>(key_idx);
                    for (std::size_t k = 0; k < hit.body_len; ++k) {
                        ASSERT_EQ(body_buf[k], expected)
                            << "Torn content or bound violation on collided key!";
                    }
                }
            }
        });
    }

    gate.release();
    for (auto& t : threads) {
        t.join();
    }

    std::size_t active_count = 0;
    for (std::size_t j = 0; j < kL1SlotsPerShard; ++j) {
        if (l1.shards[0].slots[j].key[0] != '\0') {
            active_count++;
        }
    }
    EXPECT_LE(active_count, 16);
}

} // namespace bytetaper::cache

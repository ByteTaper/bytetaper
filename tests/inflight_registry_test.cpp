// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/inflight_registry.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::coalescing {

class InFlightRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_init(&registry);
    }

    InFlightRegistry registry;
};

TEST_F(InFlightRegistryTest, LeaderFollowerBasic) {
    const char* key = "test-key";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    // First request is Leader
    auto res1 = registry_register(&registry, key, now, window, max_waiters);
    EXPECT_EQ(res1.role, InFlightRole::Leader);

    // Second request is Follower
    auto res2 = registry_register(&registry, key, now + 1, window, max_waiters);
    EXPECT_EQ(res2.role, InFlightRole::Follower);
}

TEST_F(InFlightRegistryTest, DifferentKeysAreLeaders) {
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    EXPECT_EQ(registry_register(&registry, "key1", now, window, max_waiters).role,
              InFlightRole::Leader);
    EXPECT_EQ(registry_register(&registry, "key2", now, window, max_waiters).role,
              InFlightRole::Leader);
}

TEST_F(InFlightRegistryTest, ExpiryBecomesLeader) {
    const char* key = "test-key";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    registry_register(&registry, key, now, window, max_waiters);

    // At now + 100, it should be expired (>= 1000 + 100)
    auto res = registry_register(&registry, key, now + 100, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Leader);
}

TEST_F(InFlightRegistryTest, MaxWaitersEnforcedFastFail) {
    const char* key = "test-key";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 2;

    EXPECT_EQ(registry_register(&registry, key, now, window, max_waiters).role,
              InFlightRole::Leader);
    EXPECT_EQ(registry_register(&registry, key, now, window, max_waiters).role,
              InFlightRole::Follower); // waiter 1
    EXPECT_EQ(registry_register(&registry, key, now, window, max_waiters).role,
              InFlightRole::Follower); // waiter 2

    // Third follower should be Rejected (Fast-Fail)
    EXPECT_EQ(registry_register(&registry, key, now, window, max_waiters).role,
              InFlightRole::Reject);
}

TEST_F(InFlightRegistryTest, CompletionAllowsNewLeader) {
    const char* key = "test-key";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    registry_register(&registry, key, now, window, max_waiters);
    EXPECT_EQ(registry_register(&registry, key, now, window, max_waiters).role,
              InFlightRole::Follower);

    registry_complete_state(&registry, key, InFlightCompletionState::NotCacheable, now);

    // After completion with cacheable=false, next request is Leader again
    EXPECT_EQ(registry_register(&registry, key, now, window, max_waiters).role,
              InFlightRole::Leader);
}

TEST_F(InFlightRegistryTest, CacheableCompletionAllowsFollower) {
    const char* key = "c_key:test:1:/api";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    registry_register(&registry, key, now, window, max_waiters);
    registry_complete_state(&registry, key, InFlightCompletionState::Stored, now);

    // After completion with cacheable=true, next request within window is Follower
    EXPECT_EQ(registry_register(&registry, key, now + 10, window, max_waiters).role,
              InFlightRole::Follower);
}

TEST_F(InFlightRegistryTest, CacheableCompletionExpiresToLeader) {
    const char* key = "c_key:test:1:/api";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    registry_register(&registry, key, now, window, max_waiters);
    registry_complete_state(&registry, key, InFlightCompletionState::Stored, now);

    // After completion window expires, next request is Leader again
    EXPECT_EQ(registry_register(&registry, key, now + window + 1, window, max_waiters).role,
              InFlightRole::Leader);
}

TEST_F(InFlightRegistryTest, ShardFullRejects) {
    auto hash_fn = [](const char* s) -> std::uint64_t {
        std::uint64_t hash = 14695981039346656037ULL;
        while (*s) {
            hash ^= static_cast<std::uint64_t>(*s++);
            hash *= 1099511628211ULL;
        }
        return hash;
    };

    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    // Find keys that hash to the same shard (index 0)
    char keys[kSlotsPerShard][32];
    int found = 0;
    for (int i = 0; found < (int) kSlotsPerShard; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "key%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            strcpy(keys[found], buf);
            found++;
        }
    }

    // Fill the shard
    for (int i = 0; i < (int) kSlotsPerShard; ++i) {
        EXPECT_EQ(registry_register(&registry, keys[i], 1000, window, max_waiters).role,
                  InFlightRole::Leader);
    }

    // 17th key to the same shard should be Rejected
    for (int i = 0;; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "extra%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            EXPECT_EQ(registry_register(&registry, buf, 1000, window, max_waiters).role,
                      InFlightRole::Reject);
            break;
        }
    }
}

TEST_F(InFlightRegistryTest, RemoveWaiterDecrementsCount) {
    const char* key = "c_key:test:1:/api";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 1;

    // 1. Leader
    registry_register(&registry, key, now, window, max_waiters);

    // 2. Follower 1
    auto res = registry_register(&registry, key, now, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Follower);

    // 3. Follower 2 (Rejected)
    auto res2 = registry_register(&registry, key, now, window, max_waiters);
    EXPECT_EQ(res2.role, InFlightRole::Reject);

    // 5. Remove Follower 1
    registry_remove_waiter(&registry, key);

    // 6. Follower 2 can now join
    auto res3 = registry_register(&registry, key, now, window, max_waiters);
    EXPECT_EQ(res3.role, InFlightRole::Follower);
}

TEST_F(InFlightRegistryTest, ExpiredReturnedOnGenMismatch) {
    const char* key = "test-key";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    auto res = registry_register(&registry, key, now, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Leader);

    // Mismatched expected generation should return Expired
    RegistrySharedResponseOutput resp{};
    auto wait_res =
        registry_wait_for_completion(&registry, key, 50, res.lifecycle_generation + 1, &resp);
    EXPECT_EQ(wait_res, RegistryWaitResult::Expired);
}

TEST_F(InFlightRegistryTest, ExpiredReturnedWhenInactiveDuringWait) {
    const char* key = "test-key";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    auto res = registry_register(&registry, key, now, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Leader);

    // Start a background thread to wait for completion
    std::atomic<RegistryWaitResult> wait_res{ RegistryWaitResult::Timeout };
    std::atomic<bool> wait_done{ false };
    std::thread t([this, key, res, &wait_res, &wait_done]() {
        RegistrySharedResponseOutput resp{};
        wait_res =
            registry_wait_for_completion(&registry, key, 1000, res.lifecycle_generation, &resp);
        wait_done = true;
    });

    // Let the other thread enter the wait
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Simulate entry deactivation (active = false) or generation mismatch during the wait
    {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const char* p = key; *p; ++p) {
            hash ^= static_cast<std::uint64_t>(*p);
            hash *= 1099511628211ULL;
        }
        auto& shard = registry.shards[hash % kInFlightShards];
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            for (std::size_t j = 0; j < kSlotsPerShard; ++j) {
                if (shard.slots[j].active && std::strcmp(shard.slots[j].key, key) == 0) {
                    shard.slots[j].lifecycle_generation++; // change generation
                }
            }
        }
        shard.cv.notify_all();
    }

    t.join();
    EXPECT_TRUE(wait_done);
    EXPECT_EQ(wait_res.load(), RegistryWaitResult::Expired);
}

TEST_F(InFlightRegistryTest, MissingStillReturnedWhenNoEntryFound) {
    const char* key = "nonexistent-key";
    RegistrySharedResponseOutput resp{};
    auto wait_res = registry_wait_for_completion(&registry, key, 50, 1, &resp);
    EXPECT_EQ(wait_res, RegistryWaitResult::Missing);
}

TEST_F(InFlightRegistryTest, ReclaimsAfterGraceWindow) {
    auto hash_fn = [](const char* s) -> std::uint64_t {
        std::uint64_t hash = 14695981039346656037ULL;
        while (*s) {
            hash ^= static_cast<std::uint64_t>(*s++);
            hash *= 1099511628211ULL;
        }
        return hash;
    };

    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    // Find 16 keys that hash to shard index 0
    char keys[kSlotsPerShard][32];
    int found = 0;
    for (int i = 0; found < (int) kSlotsPerShard; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "key%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            strcpy(keys[found], buf);
            found++;
        }
    }

    // Register all of them at now=1000
    for (int i = 0; i < (int) kSlotsPerShard; ++i) {
        EXPECT_EQ(registry_register(&registry, keys[i], 1000, window, max_waiters).role,
                  InFlightRole::Leader);
    }

    // Complete all of them as Stored at completed_at=1050
    for (int i = 0; i < (int) kSlotsPerShard; ++i) {
        EXPECT_TRUE(
            registry_complete_state(&registry, keys[i], InFlightCompletionState::Stored, 1050));
    }

    // Now advance time to 1200 (which is >= 1050 + 100)
    // Find a 17th key that hashes to shard index 0
    char extra_key[32] = "";
    for (int i = 0;; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "extra%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            strcpy(extra_key, buf);
            break;
        }
    }

    // Since the slots are expired terminal entries, registering the 17th key should reclaim one
    // slot and succeed as Leader!
    auto res = registry_register(&registry, extra_key, 1200, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Leader);
}

TEST_F(InFlightRegistryTest, DoesNotReclaimWithinGraceWindow) {
    const char* key = "test-key-within-grace";
    std::uint64_t now = 1000;
    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    // Register key A
    registry_register(&registry, key, now, window, max_waiters);
    registry_complete_state(&registry, key, InFlightCompletionState::Stored, now + 10);

    // Re-register key A within wait_window_ms (at now + 50 < 1010 + 100)
    // Expect to join as Follower
    auto res = registry_register(&registry, key, now + 50, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Follower);
}

TEST_F(InFlightRegistryTest, DifferentKeyReclaimsExpiredTerminalSlot) {
    auto hash_fn = [](const char* s) -> std::uint64_t {
        std::uint64_t hash = 14695981039346656037ULL;
        while (*s) {
            hash ^= static_cast<std::uint64_t>(*s++);
            hash *= 1099511628211ULL;
        }
        return hash;
    };

    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    // Find two keys that map to same shard
    char key_a[32] = "";
    char key_b[32] = "";
    int found = 0;
    for (int i = 0; found < 2; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "pair%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            if (found == 0) {
                strcpy(key_a, buf);
            } else {
                strcpy(key_b, buf);
            }
            found++;
        }
    }

    // Register Key A at now=1000
    registry_register(&registry, key_a, 1000, window, max_waiters);
    registry_complete_state(&registry, key_a, InFlightCompletionState::Stored, 1010);

    // Register Key B at 1200 (past grace)
    // Key B should reclaim Key A's slot and become Leader
    auto res = registry_register(&registry, key_b, 1200, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Leader);
}

TEST_F(InFlightRegistryTest, DoesNotReclaimWithActiveWaiter) {
    auto hash_fn = [](const char* s) -> std::uint64_t {
        std::uint64_t hash = 14695981039346656037ULL;
        while (*s) {
            hash ^= static_cast<std::uint64_t>(*s++);
            hash *= 1099511628211ULL;
        }
        return hash;
    };

    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    // Find 16 keys that hash to shard index 0
    char keys[kSlotsPerShard][32];
    int found = 0;
    for (int i = 0; found < (int) kSlotsPerShard; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "key%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            strcpy(keys[found], buf);
            found++;
        }
    }

    // Register all 16 keys
    for (int i = 0; i < (int) kSlotsPerShard; ++i) {
        registry_register(&registry, keys[i], 1000, window, max_waiters);
    }

    // Add a follower/waiter to the first key (waiter_count becomes 1)
    registry_register(&registry, keys[0], 1005, window, max_waiters);

    // Complete all 16 keys at 1050
    for (int i = 0; i < (int) kSlotsPerShard; ++i) {
        registry_complete_state(&registry, keys[i], InFlightCompletionState::Stored, 1050);
    }

    // Advance to 1200 (past grace window)
    // Find a 17th key
    char extra_key[32] = "";
    for (int i = 0;; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "extra%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            strcpy(extra_key, buf);
            break;
        }
    }

    // Try to register the 17th key.
    // 15 slots can be reclaimed, but keys[0] has an active waiter, so keys[0]'s slot must NOT be
    // reclaimed! We should still be able to find one of the other 15 slots to reclaim and succeed
    // as Leader.
    auto res = registry_register(&registry, extra_key, 1200, window, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Leader);

    // Now, we register 14 more extra keys to consume all 15 reclaimable slots
    for (int i = 1; i < (int) kSlotsPerShard - 1; ++i) {
        char extra_buf[32];
        int extra_found = 0;
        for (int k = 0;; ++k) {
            char b[32];
            std::snprintf(b, sizeof(b), "reclaim%d-%d", i, k);
            if ((hash_fn(b) % kInFlightShards) == 0) {
                strcpy(extra_buf, b);
                // Register should succeed
                auto sub_res = registry_register(&registry, extra_buf, 1200, window, max_waiters);
                if (sub_res.role == InFlightRole::Leader) {
                    extra_found = 1;
                    break;
                }
            }
        }
        EXPECT_EQ(extra_found, 1);
    }

    // Now all 15 reclaimable slots are occupied by the new keys.
    // The only slot that was NOT reclaimed is the keys[0] slot because it has an active waiter.
    // Therefore, trying to register one more key should result in REJECT!
    char final_key[32] = "";
    for (int i = 0;; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "final%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            strcpy(final_key, buf);
            break;
        }
    }
    auto final_res = registry_register(&registry, final_key, 1200, window, max_waiters);
    EXPECT_EQ(final_res.role, InFlightRole::Reject);
}

TEST_F(InFlightRegistryTest, LifecycleGenerationSafeAfterReuse) {
    auto hash_fn = [](const char* s) -> std::uint64_t {
        std::uint64_t hash = 14695981039346656037ULL;
        while (*s) {
            hash ^= static_cast<std::uint64_t>(*s++);
            hash *= 1099511628211ULL;
        }
        return hash;
    };

    std::uint32_t window = 100;
    std::uint32_t max_waiters = 5;

    // Find key A and key B that map to the same shard
    char key_a[32] = "";
    char key_b[32] = "";
    int found = 0;
    for (int i = 0; found < 2; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "genpair%d", i);
        if ((hash_fn(buf) % kInFlightShards) == 0) {
            if (found == 0) {
                strcpy(key_a, buf);
            } else {
                strcpy(key_b, buf);
            }
            found++;
        }
    }

    // Register Key A
    auto reg_a = registry_register(&registry, key_a, 1000, window, max_waiters);
    EXPECT_EQ(reg_a.role, InFlightRole::Leader);
    std::uint64_t gen_a = reg_a.lifecycle_generation;

    // Complete Key A
    registry_complete_state(&registry, key_a, InFlightCompletionState::Stored, 1010);

    // Advance time and register Key B (which reclaims Key A's slot)
    auto reg_b = registry_register(&registry, key_b, 1200, window, max_waiters);
    EXPECT_EQ(reg_b.role, InFlightRole::Leader);

    // Stale wait call using Key A with generation A should return Expired (or Missing) since the
    // slot is now used for Key B
    RegistrySharedResponseOutput resp{};
    auto wait_res = registry_wait_for_completion(&registry, key_a, 50, gen_a, &resp);
    EXPECT_TRUE(wait_res == RegistryWaitResult::Expired || wait_res == RegistryWaitResult::Missing);
}

} // namespace bytetaper::coalescing

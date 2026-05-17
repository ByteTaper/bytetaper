// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/inflight_registry.h"
#include "concurrency_test_barrier.h"
#include "hash/hash.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace bytetaper::coalescing {

class InFlightRegistryWaiterAccountingConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
        registry_init(&registry);
    }

    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    InFlightRegistry registry;
};

TEST_F(InFlightRegistryWaiterAccountingConcurrencyTest, WaiterCountDoesNotUnderflow) {
    const char* key = "waiter_key";
    std::uint64_t now_ms = 1000;
    std::uint32_t wait_window_ms = 100;
    std::uint32_t max_waiters = 10;

    // 1. Register 1 leader + 8 followers
    auto reg_leader = registry_register(&registry, key, now_ms, wait_window_ms, max_waiters);
    EXPECT_EQ(reg_leader.role, InFlightRole::Leader);
    std::uint64_t gen_leader = reg_leader.lifecycle_generation;

    for (int i = 0; i < 8; ++i) {
        auto reg_follower = registry_register(&registry, key, now_ms, wait_window_ms, max_waiters);
        EXPECT_EQ(reg_follower.role, InFlightRole::Follower);
    }

    // 2. Complete leader with TooLargeForHandoff
    bool comp = registry_complete_state(&registry, key, InFlightCompletionState::TooLargeForHandoff,
                                        now_ms + 10);
    EXPECT_TRUE(comp);

    // 3. Start 8 threads
    constexpr int kFollowers = 8;
    std::vector<std::thread> threads;
    StartGate gate;

    for (int i = 0; i < kFollowers; ++i) {
        threads.emplace_back([this, key, gen_leader, &gate] {
            gate.arrive_and_wait(kFollowers);

            registry_enter_wait(&registry, key);
            auto res = registry_wait_for_completion(&registry, key, 100, gen_leader, nullptr);
            EXPECT_EQ(res, RegistryWaitResult::TooLargeForHandoff);
            registry_exit_wait(&registry, key);

            registry_remove_waiter(&registry, key);
        });
    }

    gate.release();
    for (auto& t : threads) {
        t.join();
    }

    // 4. Assert active waiters are 0
    EXPECT_EQ(registry_shard_active_waiters(&registry, key), 0);
    EXPECT_EQ(registry_active_waiters(&registry), 0);
}

TEST_F(InFlightRegistryWaiterAccountingConcurrencyTest, DoubleDeregisterIsIdempotent) {
    const char* key = "double_dereg_key";
    std::uint64_t now_ms = 1000;
    std::uint32_t wait_window_ms = 100;
    std::uint32_t max_waiters = 5;

    // Register 1 leader + 1 follower
    registry_register(&registry, key, now_ms, wait_window_ms, max_waiters);
    registry_register(&registry, key, now_ms, wait_window_ms, max_waiters);

    // Call registry_remove_waiter twice from two threads
    std::vector<std::thread> threads;
    StartGate gate;

    for (int i = 0; i < 2; ++i) {
        threads.emplace_back([this, key, &gate] {
            gate.arrive_and_wait(2);
            registry_remove_waiter(&registry, key);
        });
    }

    gate.release();
    for (auto& t : threads) {
        t.join();
    }

    // Call it a third and fourth time manually to check
    registry_remove_waiter(&registry, key);
    registry_remove_waiter(&registry, key);

    // If waiter_count underflowed to UINT32_MAX, slot.waiter_count would be UINT32_MAX and we
    // wouldn't be able to register another follower.
    // Verify we can register a follower now:
    auto res = registry_register(&registry, key, now_ms, wait_window_ms, max_waiters);
    EXPECT_EQ(res.role, InFlightRole::Follower);
}

} // namespace bytetaper::coalescing

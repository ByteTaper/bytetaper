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

class InFlightRegistryGenerationConcurrencyTest : public ::testing::Test {
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

TEST_F(InFlightRegistryGenerationConcurrencyTest, StaleGenerationIsRejected) {
    const char* key = "stale_gen_key";
    std::uint64_t now_ms = 1000;
    std::uint32_t wait_window_ms = 100;
    std::uint32_t max_waiters = 5;

    // 1. Register leader A for key K -> capture gen_a
    auto reg_a = registry_register(&registry, key, now_ms, wait_window_ms, max_waiters);
    EXPECT_EQ(reg_a.role, InFlightRole::Leader);
    std::uint64_t gen_a = reg_a.lifecycle_generation;

    // 2. Let wait window expire (pass future now_ms) and re-register -> leader B, capture gen_b
    std::uint64_t future_now_ms = now_ms + wait_window_ms + 10;
    auto reg_b = registry_register(&registry, key, future_now_ms, wait_window_ms, max_waiters);
    EXPECT_EQ(reg_b.role, InFlightRole::Leader);
    std::uint64_t gen_b = reg_b.lifecycle_generation;

    EXPECT_NE(gen_a, gen_b);

    // 2b. Register a follower specifically for generation B
    auto reg_follower_b =
        registry_register(&registry, key, future_now_ms, wait_window_ms, max_waiters);
    EXPECT_EQ(reg_follower_b.role, InFlightRole::Follower);
    EXPECT_EQ(reg_follower_b.lifecycle_generation, gen_b);

    // Spawn a follower thread to observe the terminal state of generation B concurrently
    std::thread follower_thread([this, key, wait_window_ms, gen_b] {
        registry_enter_wait(&registry, key);
        RegistrySharedResponseOutput response_out{};
        RegistryWaitResult wait_res =
            registry_wait_for_completion(&registry, key, wait_window_ms, gen_b, &response_out);
        EXPECT_EQ(wait_res, RegistryWaitResult::NotCacheable);
        registry_exit_wait(&registry, key);
    });

    // Deterministically yield until the follower thread has successfully registered the wait state
    while (registry_shard_active_waiters(&registry, key) == 0) {
        std::this_thread::yield();
    }

    // 3. Call registry_complete_state_if_generation for gen_a -> expect false
    bool comp_a = registry_complete_state_if_generation(
        &registry, key, gen_a, InFlightCompletionState::NotCacheable, future_now_ms);
    EXPECT_FALSE(comp_a);

    // 4. Call registry_complete_state_if_generation for gen_b -> expect true
    bool comp_b = registry_complete_state_if_generation(
        &registry, key, gen_b, InFlightCompletionState::NotCacheable, future_now_ms);
    EXPECT_TRUE(comp_b);

    follower_thread.join();
}

TEST_F(InFlightRegistryGenerationConcurrencyTest, ConcurrentRegisterAndCompleteDoesNotCorrupt) {
    constexpr int kThreadCount = 4;
    std::vector<std::thread> threads;
    StartGate gate;

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([this, i, &gate] {
            gate.arrive_and_wait(kThreadCount);

            std::string key = "K_" + std::to_string(i);
            std::uint64_t now_ms = 1000;
            std::uint32_t wait_window_ms = 100;
            std::uint32_t max_waiters = 5;

            auto reg =
                registry_register(&registry, key.c_str(), now_ms, wait_window_ms, max_waiters);
            EXPECT_EQ(reg.role, InFlightRole::Leader);

            bool comp = registry_complete_state_if_generation(
                &registry, key.c_str(), reg.lifecycle_generation,
                InFlightCompletionState::NotCacheable, now_ms + 10);
            EXPECT_TRUE(comp);
        });
    }

    gate.release();
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(registry_active_waiters(&registry), 0);
}

} // namespace bytetaper::coalescing

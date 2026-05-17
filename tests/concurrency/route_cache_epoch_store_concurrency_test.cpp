// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "concurrency_test_barrier.h"
#include "runtime/route_cache_epoch_store.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace bytetaper::runtime {

class RouteCacheEpochStoreConcurrencyTest : public ::testing::Test {
protected:
    RouteCacheEpochStore store;
};

TEST_F(RouteCacheEpochStoreConcurrencyTest, ConcurrentBumpExactCount) {
    RouteCacheEpochResult reg_res = route_cache_epoch_register(&store, "route-0");
    ASSERT_EQ(reg_res, RouteCacheEpochResult::Ok);

    constexpr int kThreads = 8;
    constexpr int kBumpsPerThread = 100;
    std::vector<std::thread> threads;
    StartGate gate;

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([this, &gate] {
            gate.arrive_and_wait(kThreads);
            for (int j = 0; j < kBumpsPerThread; ++j) {
                std::uint64_t dummy;
                RouteCacheEpochResult res = route_cache_epoch_bump(&store, "route-0", &dummy);
                EXPECT_EQ(res, RouteCacheEpochResult::Ok);
            }
        });
    }

    gate.release();
    for (auto& t : threads) {
        t.join();
    }

    std::uint64_t epoch = 0;
    RouteCacheEpochResult get_res = route_cache_epoch_get(&store, "route-0", &epoch);
    ASSERT_EQ(get_res, RouteCacheEpochResult::Ok);
    EXPECT_EQ(epoch, kInitialRouteCacheEpoch + 800);
}

TEST_F(RouteCacheEpochStoreConcurrencyTest, ConcurrentReadDuringBump) {
    RouteCacheEpochResult reg_res = route_cache_epoch_register(&store, "route-0");
    ASSERT_EQ(reg_res, RouteCacheEpochResult::Ok);

    constexpr int kBumpers = 4;
    constexpr int kReaders = 4;
    constexpr int kTotal = kBumpers + kReaders;
    std::vector<std::thread> threads;
    StartGate gate;

    // 4 bumper threads
    for (int i = 0; i < kBumpers; ++i) {
        threads.emplace_back([this, &gate] {
            gate.arrive_and_wait(kTotal);
            for (int j = 0; j < 500; ++j) {
                std::uint64_t dummy;
                route_cache_epoch_bump(&store, "route-0", &dummy);
            }
        });
    }

    // 4 reader threads
    for (int i = 0; i < kReaders; ++i) {
        threads.emplace_back([this, &gate] {
            gate.arrive_and_wait(kTotal);
            for (int j = 0; j < 500; ++j) {
                std::uint64_t epoch = 0;
                route_cache_epoch_get(&store, "route-0", &epoch);
                EXPECT_GE(epoch, kInitialRouteCacheEpoch);
            }
        });
    }

    gate.release();
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace bytetaper::runtime

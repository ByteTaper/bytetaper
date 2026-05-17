// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "concurrency_test_barrier.h"
#include "runtime/policy_snapshot.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace bytetaper::runtime {

class RuntimePolicyStoreConcurrencyTest : public ::testing::Test {
protected:
    RuntimePolicyStore store;
};

TEST_F(RuntimePolicyStoreConcurrencyTest, ConcurrentLoadAndSwap) {
    // 1. Build minimal snapshot with 0 routes
    auto build_res = build_runtime_policy_snapshot_from_routes(nullptr, 0, "empty.yaml", 1);
    ASSERT_TRUE(build_res.ok);
    auto snap0 = build_res.snapshot;

    std::string err;
    // 2. Install initial
    ASSERT_TRUE(store.install_initial(snap0, &err));

    constexpr int kReaders = 8;
    constexpr int kTotalThreads = kReaders + 1;
    std::vector<std::thread> threads;
    StartGate gate;

    // 3. Start 8 reader threads
    for (int i = 0; i < kReaders; ++i) {
        threads.emplace_back([this, &gate, snap0] {
            gate.arrive_and_wait(kTotalThreads);
            std::uint64_t last_gen = 0;
            for (int j = 0; j < 1000; ++j) {
                auto s = store.load();
                ASSERT_NE(s, nullptr);

                // Inspect immutable fields
                EXPECT_EQ(s->source_name, "empty.yaml");
                EXPECT_TRUE(s->routes.empty());
                EXPECT_FALSE(s->policy_identity.empty());
                EXPECT_EQ(s->policy_identity, snap0->policy_identity);

                // Assert generation monotonicity
                ASSERT_GE(s->generation, last_gen);
                last_gen = s->generation;
            }
        });
    }

    // 4. Start 1 writer thread
    threads.emplace_back([this, &gate] {
        gate.arrive_and_wait(kTotalThreads);
        for (int j = 0; j < 100; ++j) {
            auto lock = store.acquire_apply_lock();
            std::string local_err;
            // Build a new snapshot with different generation
            auto build_res_new =
                build_runtime_policy_snapshot_from_routes(nullptr, 0, "empty.yaml", 2 + j);
            ASSERT_TRUE(build_res_new.ok);
            store.swap(build_res_new.snapshot, &local_err);
        }
    });

    gate.release();
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace bytetaper::runtime

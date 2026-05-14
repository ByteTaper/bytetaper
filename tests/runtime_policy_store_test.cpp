// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/yaml_loader.h"
#include "runtime/policy_snapshot.h"

#include <gtest/gtest.h>
#include <thread>

namespace bytetaper::runtime {

TEST(RuntimePolicyStoreTest, LifecycleAndThreadSafety) {
    RuntimePolicyStore store{};

    // 1. Initial store state is empty
    EXPECT_EQ(store.load(), nullptr);

    // 2. Generation increments
    EXPECT_EQ(store.next_generation(), 1);
    EXPECT_EQ(store.next_generation(), 2);

    // 3. Install initial snapshot
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "initial-r";
    raw_routes[0].match_prefix = "/";

    auto build_res1 = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 2);
    ASSERT_TRUE(build_res1.ok);

    std::string err;
    EXPECT_TRUE(store.install_initial(build_res1.snapshot, &err));

    // Secondary install_initial should fail
    EXPECT_FALSE(store.install_initial(build_res1.snapshot, &err));
    EXPECT_EQ(err, "store already initialized");

    // 4. Verify initial loaded snapshot
    auto loaded1 = store.load();
    ASSERT_NE(loaded1, nullptr);
    EXPECT_EQ(loaded1->generation, 2);
    EXPECT_STREQ(loaded1->routes[0].route_id, "initial-r");

    // 5. Swap with new snapshot
    raw_routes[0].route_id = "new-r";
    auto build_res2 = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 3);
    ASSERT_TRUE(build_res2.ok);

    EXPECT_TRUE(store.swap(build_res2.snapshot, &err));

    auto loaded2 = store.load();
    ASSERT_NE(loaded2, nullptr);
    EXPECT_EQ(loaded2->generation, 3);
    EXPECT_STREQ(loaded2->routes[0].route_id, "new-r");

    // Proves snapshot-ownership and reference-counting safety:
    // Swapping out the snapshot inside the store does not affect existing, pinned shared_ptr
    // references!
    ASSERT_NE(loaded1, nullptr);
    EXPECT_EQ(loaded1->generation, 2);
    EXPECT_STREQ(loaded1->routes[0].route_id, "initial-r");

    // 6. Concurrency Stress Test to ensure no race conditions
    constexpr int kThreadCount = 8;
    constexpr int kIterations = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&store, build_res2]() {
            for (int j = 0; j < kIterations; ++j) {
                // Read snapshot
                auto s = store.load();
                EXPECT_NE(s, nullptr);

                // Write generation
                store.next_generation();

                // Swap snapshot back and forth
                std::string local_err;
                store.swap(build_res2.snapshot, &local_err);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

TEST(RuntimePolicyStoreTest, StartupPolicyFileConversion) {
    const char* yaml = R"(
routes:
  - id: "startup-route-id"
    match:
      kind: "prefix"
      prefix: "/api/v1/"
    mutation: "full"
)";

    policy::PolicyFileResult policy_result{};
    ASSERT_TRUE(policy::load_policy_from_string(yaml, &policy_result));
    ASSERT_TRUE(policy_result.ok);
    ASSERT_EQ(policy_result.count, 1u);

    RuntimePolicyStore store{};
    EXPECT_EQ(store.load(), nullptr);

    auto build_res =
        build_runtime_policy_snapshot_from_routes(policy_result.policies, policy_result.count,
                                                  "startup_policy.yaml", store.next_generation());
    ASSERT_TRUE(build_res.ok);
    ASSERT_NE(build_res.snapshot, nullptr);

    // Verify snapshot identity is successfully computed and set during creation
    EXPECT_FALSE(build_res.snapshot->policy_identity.empty());

    // Verify converted routes have their policy_identity repopulated
    EXPECT_FALSE(build_res.snapshot->routes.empty());
    EXPECT_STRNE(build_res.snapshot->routes[0].policy_identity, "");

    std::string err;
    ASSERT_TRUE(store.install_initial(build_res.snapshot, &err));

    auto loaded = store.load();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->generation, 1);
    EXPECT_EQ(loaded->policy_identity, build_res.snapshot->policy_identity);
}

TEST(RuntimePolicyStoreTest, InstallAndLoadInitialSnapshot) {
    RuntimePolicyStore store{};
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "initial-r";
    raw_routes[0].match_prefix = "/";

    auto build_res = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 100);
    ASSERT_TRUE(build_res.ok);

    std::string err;
    EXPECT_TRUE(store.install_initial(build_res.snapshot, &err));

    auto loaded = store.load();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->generation, 100u);
    EXPECT_EQ(loaded->policy_identity, build_res.snapshot->policy_identity);
}

TEST(RuntimePolicyStoreTest, SwapChangesActiveSnapshot) {
    RuntimePolicyStore store{};
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "r1";
    raw_routes[0].match_prefix = "/1";

    auto resA = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 1);
    std::string err;
    ASSERT_TRUE(store.install_initial(resA.snapshot, &err));

    raw_routes[0].route_id = "r2";
    raw_routes[0].match_prefix = "/2";
    auto resB = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 2);

    EXPECT_TRUE(store.swap(resB.snapshot, &err));
    auto loaded = store.load();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->generation, 2u);
    EXPECT_EQ(loaded->policy_identity, resB.snapshot->policy_identity);
}

TEST(RuntimePolicyStoreTest, OldSnapshotRemainsAliveAfterSwap) {
    RuntimePolicyStore store{};
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "r1";
    raw_routes[0].match_prefix = "/1";

    auto resA = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 1);
    std::string err;
    ASSERT_TRUE(store.install_initial(resA.snapshot, &err));

    auto held_A = store.load();
    ASSERT_NE(held_A, nullptr);

    raw_routes[0].route_id = "r2";
    auto resB = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 2);
    EXPECT_TRUE(store.swap(resB.snapshot, &err));

    // held_A shared_ptr must still be fully valid and safe to read
    EXPECT_STREQ(held_A->routes[0].route_id, "r1");
    EXPECT_EQ(held_A->generation, 1u);
}

TEST(RuntimePolicyStoreTest, MultipleReadersLoadSafely) {
    RuntimePolicyStore store{};
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "r1";
    raw_routes[0].match_prefix = "/1";
    auto resA = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 1);
    std::string err;
    ASSERT_TRUE(store.install_initial(resA.snapshot, &err));

    constexpr int kThreads = 10;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&store]() {
            for (int j = 0; j < 500; ++j) {
                auto s = store.load();
                EXPECT_NE(s, nullptr);
                EXPECT_STREQ(s->routes[0].route_id, "r1");
            }
        });
    }
    for (auto& t : threads)
        t.join();
}

TEST(RuntimePolicyStoreTest, SwapWhileReadersHoldOldSnapshot) {
    RuntimePolicyStore store{};
    policy::RoutePolicy raw_routes[1]{};
    raw_routes[0].route_id = "rA";
    raw_routes[0].match_prefix = "/A";
    auto resA = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 1);
    std::string err;
    ASSERT_TRUE(store.install_initial(resA.snapshot, &err));

    // A reader takes a snapshot
    auto reader_snap = store.load();

    // A writer performs a swap
    raw_routes[0].route_id = "rB";
    raw_routes[0].match_prefix = "/B";
    auto resB = build_runtime_policy_snapshot_from_routes(raw_routes, 1, "config.yaml", 2);
    EXPECT_TRUE(store.swap(resB.snapshot, &err));

    // The old reader still sees A
    EXPECT_STREQ(reader_snap->routes[0].route_id, "rA");

    // A new reader sees B
    auto new_reader_snap = store.load();
    EXPECT_STREQ(new_reader_snap->routes[0].route_id, "rB");
}

} // namespace bytetaper::runtime

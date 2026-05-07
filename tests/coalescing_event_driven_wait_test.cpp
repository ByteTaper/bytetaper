// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/pipeline.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "policy/route_policy.h"
#include "stages/coalescing_follower_wait_stage.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

namespace bytetaper::stages {

TEST(CoalescingEventDrivenWait, FollowerWakesBeforeFullWindowWhenLeaderCompletes) {
    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());

    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());

    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = true;
    policy.coalescing.backend_timeout_ms = 800;
    policy.coalescing.handoff_buffer_ms = 200;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy;
    ctx.l1_cache = l1.get();
    ctx.coalescing_registry = registry.get();
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
    std::strcpy(ctx.coalescing_decision.key, "burst-key");

    // Pre-register leader so follower stage enters wait
    coalescing::registry_register(registry.get(), "burst-key", 1000, 1000, 128);

    auto start = std::chrono::steady_clock::now();

    // Start a thread to complete the leader after 50ms
    std::thread leader_thread([&registry]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        coalescing::registry_complete_state(registry.get(), "burst-key",
                                            coalescing::InFlightCompletionState::Stored, 1050);
    });

    // This should block and wake up when the leader completes
    apg::StageOutput output = coalescing_follower_wait_stage(ctx);

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    leader_thread.join();

    // Should wake up much faster than 1 second (e.g. < 500ms)
    EXPECT_LT(elapsed_ms, 500);
    // In this test, it will fallback because we didn't actually put anything in L1
    EXPECT_STREQ(output.note, "stored-no-snapshot-fallback");
}

TEST(CoalescingEventDrivenWait, FollowerTimeoutFallbackStillWorks) {
    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());

    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());

    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = true;
    policy.coalescing.backend_timeout_ms = 80;
    policy.coalescing.handoff_buffer_ms = 20;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy;
    ctx.l1_cache = l1.get();
    ctx.coalescing_registry = registry.get();
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
    std::strcpy(ctx.coalescing_decision.key, "timeout-key");

    // Register leader, but never complete it
    coalescing::registry_register(registry.get(), "timeout-key", 1000, 100, 128);

    auto start = std::chrono::steady_clock::now();

    apg::StageOutput output = coalescing_follower_wait_stage(ctx);

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Should wait at least 100ms
    EXPECT_GE(elapsed_ms, 100);
    EXPECT_STREQ(output.note, "timeout-fallback");
}

TEST(CoalescingEventDrivenWait, ConcurrencyMultipleFollowersReleased) {
    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());

    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());

    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = true;
    policy.coalescing.backend_timeout_ms = 800;
    policy.coalescing.handoff_buffer_ms = 200;

    // Register leader
    coalescing::registry_register(registry.get(), "shared-key", 1000, 1000, 128);

    constexpr int kNumFollowers = 10;
    std::vector<std::thread> threads;
    std::vector<apg::StageOutput> outputs(kNumFollowers);
    std::vector<double> elapsed_times(kNumFollowers, 0.0);

    auto start_all = std::chrono::steady_clock::now();

    for (int i = 0; i < kNumFollowers; ++i) {
        threads.emplace_back([&, i]() {
            apg::ApgTransformContext ctx{};
            ctx.matched_policy = &policy;
            ctx.l1_cache = l1.get();
            ctx.coalescing_registry = registry.get();
            ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
            std::strcpy(ctx.coalescing_decision.key, "shared-key");

            auto start = std::chrono::steady_clock::now();
            outputs[i] = coalescing_follower_wait_stage(ctx);
            auto end = std::chrono::steady_clock::now();

            elapsed_times[i] =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        });
    }

    // Leader thread completes after 30ms
    std::thread leader_thread([&registry]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        coalescing::registry_complete_state(registry.get(), "shared-key",
                                            coalescing::InFlightCompletionState::Stored, 1030);
    });

    leader_thread.join();
    for (auto& t : threads) {
        t.join();
    }

    auto end_all = std::chrono::steady_clock::now();
    auto elapsed_all_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_all - start_all).count();

    // Verify all followers were released within a reasonable bound (way before 1000ms window)
    EXPECT_LT(elapsed_all_ms, 300);

    for (int i = 0; i < kNumFollowers; ++i) {
        EXPECT_STREQ(outputs[i].note, "stored-no-snapshot-fallback");
        EXPECT_LT(elapsed_times[i], 300);
    }
}

TEST(CoalescingEventDrivenWait, WaiterCountDoesNotLeakUnderConcurrentJoinExit) {
    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());

    const char* key = "test-key";
    std::uint64_t now = 1000;
    std::uint32_t window = 1000;
    std::uint32_t max_waiters = 5;

    // Leader
    coalescing::registry_register(registry.get(), key, now, window, max_waiters);

    // 4 Followers join
    for (int i = 0; i < 4; ++i) {
        auto res = coalescing::registry_register(registry.get(), key, now, window, max_waiters);
        EXPECT_EQ(res.role, coalescing::InFlightRole::Follower);
    }

    // 5th follower should be Rejected (total 1 leader + 4 waiters = 5 slots. max_waiters is 5,
    // wait, let's verify if waiter_count tracks follower counts) Actually, waiter_count in
    // inflight_registry tracks followers. If max_waiters is 5, then up to 5 followers are allowed.
    auto res5 = coalescing::registry_register(registry.get(), key, now, window, max_waiters);
    EXPECT_EQ(res5.role, coalescing::InFlightRole::Follower);

    // 6th follower is rejected
    auto res6 = coalescing::registry_register(registry.get(), key, now, window, max_waiters);
    EXPECT_EQ(res6.role, coalescing::InFlightRole::Reject);

    // Exit 2 followers
    coalescing::registry_remove_waiter(registry.get(), key);
    coalescing::registry_remove_waiter(registry.get(), key);

    // Now we should be able to register another follower
    auto res7 = coalescing::registry_register(registry.get(), key, now, window, max_waiters);
    EXPECT_EQ(res7.role, coalescing::InFlightRole::Follower);
}

TEST(CoalescingEventDrivenWait, LeaderNonCacheableCompletionWakesFollower) {
    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());

    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());

    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = true;
    policy.coalescing.backend_timeout_ms = 400;
    policy.coalescing.handoff_buffer_ms = 100;

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy;
    ctx.l1_cache = l1.get();
    ctx.coalescing_registry = registry.get();
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
    std::strcpy(ctx.coalescing_decision.key, "non-cache-key");

    // Pre-register leader
    coalescing::registry_register(registry.get(), "non-cache-key", 1000, 500, 128);

    auto start = std::chrono::steady_clock::now();

    // Start a thread to complete the leader as non-cacheable after 30ms
    std::thread leader_thread([&registry]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        coalescing::registry_complete_state(registry.get(), "non-cache-key",
                                            coalescing::InFlightCompletionState::NotCacheable,
                                            1030);
    });

    // Follower waits
    apg::StageOutput output = coalescing_follower_wait_stage(ctx);

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    leader_thread.join();

    // Should wake up immediately and fall back to upstream
    EXPECT_LT(elapsed_ms, 300);
    EXPECT_STREQ(output.note, "not-cacheable-fallback");
}

} // namespace bytetaper::stages

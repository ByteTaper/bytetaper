// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/pipeline.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "hash/hash.h"
#include "policy/route_policy.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_follower_wait_stage.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

namespace bytetaper::stages {

class CoalescingFallbackTightenedTest : public ::testing::Test {
protected:
    void SetUp() override {
        bytetaper::hash::set_process_hash_seed_for_test(
            { 0x1234567812345678ULL, 0x8765432187654321ULL });
        registry = std::make_unique<coalescing::InFlightRegistry>();
        coalescing::registry_init(registry.get());

        l1 = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1.get());

        policy.route_id = "test-route";
        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.coalescing.enabled = true;
        policy.coalescing.backend_timeout_ms = 150;
        policy.coalescing.handoff_buffer_ms = 50;

        ctx.matched_policy = &policy;
        ctx.l1_cache = l1.get();
        ctx.coalescing_registry = registry.get();
        ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
        std::strcpy(ctx.coalescing_decision.key, "burst-key");

        std::strcpy(ctx.raw_path, "/test");
        ctx.request_method = policy::HttpMethod::Get;

        auto now = std::chrono::system_clock::now();
        ctx.request_epoch_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
    }

    void populate_l1(const char* key) {
        cache::CacheEntry entry{};
        std::strcpy(entry.key, key);
        std::strcpy(entry.content_type, "application/json");
        entry.body = "{\"status\":\"ok\"}";
        entry.body_len = std::strlen(entry.body);
        entry.created_at_epoch_ms = ctx.request_epoch_ms;
        entry.expires_at_epoch_ms = ctx.request_epoch_ms + 10000;
        cache::l1_put(l1.get(), entry);
    }

    std::uint32_t get_waiter_count(const char* key) {
        // Internal access for testing
        std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(key);
        auto& shard = registry->shards[hash % coalescing::kInFlightShards];
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (auto& slot : shard.slots) {
            if (slot.active && std::strcmp(slot.key, key) == 0) {
                return slot.waiter_count;
            }
        }
        return 0;
    }

    std::unique_ptr<coalescing::InFlightRegistry> registry;
    std::unique_ptr<cache::L1Cache> l1;
    policy::RoutePolicy policy;
    apg::ApgTransformContext ctx;
};

TEST_F(CoalescingFallbackTightenedTest, FinalL1HitSavesFollowerBeforeFallback) {
    // Register leader
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 200, 128);

    // Thread B: populate L1 after 50ms and complete leader
    std::thread leader_thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        cache::CacheKeyInput ki{};
        ki.method = ctx.request_method;
        ki.path = ctx.raw_path;
        ki.route_id = policy.route_id;
        ki.policy_version = policy.route_id;
        char key[256];
        cache::build_cache_key(ki, key, sizeof(key));

        populate_l1(key);
        coalescing::registry_complete_state(registry.get(), "burst-key",
                                            coalescing::InFlightCompletionState::Stored,
                                            ctx.request_epoch_ms + 50);
    });

    auto start = std::chrono::steady_clock::now();
    cache_key_prepare_stage(ctx);
    apg::StageOutput output = coalescing_follower_wait_stage(ctx);
    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    leader_thread.join();

    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);
    EXPECT_LT(elapsed_ms, 200);
}

TEST_F(CoalescingFallbackTightenedTest, WaiterCountDecrementedOnPreWaitL1Hit) {
    // 1. Register leader
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 200, 128);
    // 2. Register follower (waiter_count becomes 1)
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 200, 128);

    EXPECT_EQ(get_waiter_count("burst-key"), 1);

    // Populate L1 so pre-wait check hits
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.path = ctx.raw_path;
    ki.route_id = policy.route_id;
    ki.policy_version = policy.route_id;
    char key[256];
    cache::build_cache_key(ki, key, sizeof(key));
    populate_l1(key);

    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);

    // Waiter count should be back to 0
    EXPECT_EQ(get_waiter_count("burst-key"), 0);
}

TEST_F(CoalescingFallbackTightenedTest, WaiterCountDecrementedOnPostWaitL1Hit) {
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 200, 128);
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 200, 128);

    EXPECT_EQ(get_waiter_count("burst-key"), 1);

    std::thread leader_thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        cache::CacheKeyInput ki{};
        ki.method = ctx.request_method;
        ki.path = ctx.raw_path;
        ki.route_id = policy.route_id;
        ki.policy_version = policy.route_id;
        char key[256];
        cache::build_cache_key(ki, key, sizeof(key));

        populate_l1(key);
        coalescing::registry_complete_state(registry.get(), "burst-key",
                                            coalescing::InFlightCompletionState::Stored,
                                            ctx.request_epoch_ms + 20);
    });

    cache_key_prepare_stage(ctx);
    coalescing_follower_wait_stage(ctx);
    leader_thread.join();

    // Waiter count should be decremented (or cleaned up if no longer active)
    EXPECT_EQ(get_waiter_count("burst-key"), 0);
}

TEST_F(CoalescingFallbackTightenedTest, WaiterCountDecrementedOnTimeout) {
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 50, 128);
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 50, 128);

    EXPECT_EQ(get_waiter_count("burst-key"), 1);

    policy.coalescing.backend_timeout_ms = 40;
    policy.coalescing.handoff_buffer_ms = 10;
    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(get_waiter_count("burst-key"), 0);
}

TEST_F(CoalescingFallbackTightenedTest, SteadyClockUsedForWait) {
    coalescing::registry_register(registry.get(), "burst-key", ctx.request_epoch_ms, 50, 128);

    policy.coalescing.backend_timeout_ms = 40;
    policy.coalescing.handoff_buffer_ms = 10;
    auto start = std::chrono::steady_clock::now();
    cache_key_prepare_stage(ctx);
    coalescing_follower_wait_stage(ctx);
    auto end = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(elapsed_ms, 50);
}

} // namespace bytetaper::stages

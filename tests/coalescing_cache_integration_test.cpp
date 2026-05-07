// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_follower_wait_stage.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace bytetaper::stages {

class CoalescingCacheIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        l1_cache_ptr_ = std::make_unique<cache::L1Cache>();
        l1_init(l1_cache_ptr_.get());
        coalescing::registry_init(&registry_);

        ctx.l1_cache = l1_cache_ptr_.get();
        ctx.coalescing_registry = &registry_;
        ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
        std::strcpy(ctx.coalescing_decision.key, "c_key:test:1:/api");

        policy.route_id = "rt1";
        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.coalescing.enabled = true;
        policy.coalescing.require_cache_enabled = true;
        ctx.matched_policy = &policy;

        std::strncpy(ctx.raw_path, "/api", sizeof(ctx.raw_path) - 1);
    }

    std::unique_ptr<cache::L1Cache> l1_cache_ptr_;
    coalescing::InFlightRegistry registry_;
    apg::ApgTransformContext ctx;
    policy::RoutePolicy policy;
};

TEST_F(CoalescingCacheIntegrationTest, FollowerObservesCacheHit) {
    // 1. Manually put entry in cache to simulate leader store
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    char key_buf[cache::kCacheKeyMaxLen] = {};
    ASSERT_TRUE(cache::build_cache_key(ki, key_buf, sizeof(key_buf)));

    cache::CacheEntry entry{};
    std::strncpy(entry.key, key_buf, cache::kCacheKeyMaxLen - 1);
    entry.status_code = 200;
    cache::l1_put(l1_cache_ptr_.get(), entry);

    // 2. Run follower wait stage
    cache_key_prepare_stage(ctx);
    auto out = coalescing_follower_wait_stage(ctx);

    // 3. Should hit L1 and skip remaining
    EXPECT_EQ(out.result, apg::StageResult::SkipRemaining);
    EXPECT_STREQ(out.note, "l1-hit");
}

TEST_F(CoalescingCacheIntegrationTest, FollowerBypassesWhenCacheDisabled) {
    policy.cache.behavior = policy::CacheBehavior::Default; // disabled

    // Manually register follower
    coalescing::registry_register(&registry_, ctx.coalescing_decision.key, 1000, 100, 5);

    auto out = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "cache-disabled-bypassed");
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Bypass);

    // Verify deregistration (waiter count should be 0)
    // We can verify this by checking if we can add 5 waiters (max)
    for (int i = 0; i < 5; ++i) {
        auto res =
            coalescing::registry_register(&registry_, ctx.coalescing_decision.key, 1000, 100, 5);
        EXPECT_EQ(res.role, coalescing::InFlightRole::Follower);
    }
}

TEST_F(CoalescingCacheIntegrationTest, FollowerTimesOutAndFallsBack) {
    policy.coalescing.backend_timeout_ms = 8;
    policy.coalescing.handoff_buffer_ms = 2;
    ctx.request_epoch_ms = 1000;

    // Manually register leader and follower
    coalescing::registry_register(&registry_, ctx.coalescing_decision.key, 1000, 10, 5);
    coalescing::registry_register(&registry_, ctx.coalescing_decision.key, 1000, 10, 5);

    auto out = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(out.result, apg::StageResult::Continue);
    EXPECT_STREQ(out.note, "timeout-fallback");
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Bypass);
}

} // namespace bytetaper::stages

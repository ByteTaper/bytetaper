// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "metrics/coalescing_metrics.h"
#include "metrics/runtime_metrics.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_follower_wait_stage.h"
#include "stages/coalescing_leader_completion_stage.h"

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
        store_.count = 0;
        runtime::route_cache_epoch_register(&store_, "rt1");

        ctx.l1_cache = l1_cache_ptr_.get();
        ctx.coalescing_registry = &registry_;
        ctx.route_cache_epoch_store = &store_;
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
    runtime::RouteCacheEpochStore store_;
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
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
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

TEST_F(CoalescingCacheIntegrationTest, LargeBodyLeaderDoesNotPublishL2ReadyToFollower) {
    metrics::CoalescingMetrics coalescing_metrics_{};
    metrics::RuntimeMetrics runtime_metrics_{};

    // We can use a unique key for this test to avoid collision
    const char* key = "c_key:test:1:/large";

    // 1. Register leader
    auto reg_leader = coalescing::registry_register(&registry_, key, 1000, 100, 5);
    ASSERT_EQ(reg_leader.role, coalescing::InFlightRole::Leader);

    // 2. Register follower
    auto reg_follower = coalescing::registry_register(&registry_, key, 1000, 100, 5);
    ASSERT_EQ(reg_follower.role, coalescing::InFlightRole::Follower);

    // 3. Leader completes with a large body (> kL2BodyBufSize, <= kL2MaxBodySize)
    apg::ApgTransformContext leader_ctx{};
    leader_ctx.l1_cache = l1_cache_ptr_.get();
    leader_ctx.coalescing_registry = &registry_;
    leader_ctx.coalescing_metrics = &coalescing_metrics_;
    leader_ctx.runtime_metrics = &runtime_metrics_;
    leader_ctx.matched_policy = &policy;
    leader_ctx.cache_key_ready = true;
    leader_ctx.response_status_code = 200;

    leader_ctx.coalescing_decision.action = coalescing::CoalescingAction::Leader;
    std::strcpy(leader_ctx.coalescing_decision.key, key);
    leader_ctx.coalescing_decision.lifecycle_generation = reg_leader.lifecycle_generation;

    // Use kL2BodyBufSize + 100 as the body size
    const std::size_t large_body_len = apg::ApgTransformContext::kL2BodyBufSize + 100;
    std::string large_body(large_body_len, 'x');
    leader_ctx.response_body = large_body.c_str();
    leader_ctx.response_body_len = large_body_len;
    std::strcpy(leader_ctx.response_content_type, "application/json");

    auto out_leader = coalescing_leader_completion_stage(leader_ctx);
    EXPECT_EQ(out_leader.result, apg::StageResult::Continue);
    EXPECT_STREQ(out_leader.note, "awaiting-l2-completion-no-handoff");

    // 4. Follower wait stage runs
    apg::ApgTransformContext follower_ctx{};
    follower_ctx.l1_cache = l1_cache_ptr_.get();
    follower_ctx.coalescing_registry = &registry_;
    follower_ctx.coalescing_metrics = &coalescing_metrics_;
    follower_ctx.runtime_metrics = &runtime_metrics_;
    follower_ctx.matched_policy = &policy;
    follower_ctx.route_cache_epoch_store = &store_;
    follower_ctx.request_epoch_ms = 1000;
    std::strncpy(follower_ctx.raw_path, "/api", sizeof(follower_ctx.raw_path) - 1);

    follower_ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
    std::strcpy(follower_ctx.coalescing_decision.key, key);
    follower_ctx.coalescing_decision.lifecycle_generation = reg_follower.lifecycle_generation;

    cache_key_prepare_stage(follower_ctx);
    auto out_follower = coalescing_follower_wait_stage(follower_ctx);

    // Follower falls back (no shared response delivered)
    EXPECT_EQ(out_follower.result, apg::StageResult::Continue);
    EXPECT_STREQ(out_follower.note, "too-large-for-handoff-fallback");
    EXPECT_EQ(coalescing_metrics_.follower_too_large_for_handoff_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics_.follower_l2_ready_total.load(), 0u);
}

} // namespace bytetaper::stages

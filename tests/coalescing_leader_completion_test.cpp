// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/inflight_registry.h"
#include "stages/coalescing_leader_completion_stage.h"

#include <chrono>
#include <gtest/gtest.h>

namespace bytetaper::stages {

class CoalescingLeaderCompletionTest : public ::testing::Test {
protected:
    void SetUp() override {
        coalescing::registry_init(&registry);

        ctx.coalescing_registry = &registry;
        ctx.coalescing_decision.action = coalescing::CoalescingAction::Leader;
        std::strcpy(ctx.coalescing_decision.key, "c_key:test:1:/api");

        policy.cache.behavior = policy::CacheBehavior::Store;
        ctx.matched_policy = &policy;
        ctx.response_status_code = 200;
    }

    coalescing::InFlightRegistry registry;
    apg::ApgTransformContext ctx;
    policy::RoutePolicy policy;
};

TEST_F(CoalescingLeaderCompletionTest, NonLeaderBypasses) {
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Bypass;
    auto output = coalescing_leader_completion_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "not-leader");
}

TEST_F(CoalescingLeaderCompletionTest, CacheableLeaderCompletes) {
    // Manually register leader
    coalescing::registry_register(&registry, ctx.coalescing_decision.key, 1000, 100, 5);

    ctx.cache_key_ready = true;
    ctx.response_body = "{\"status\":\"ok\"}";
    ctx.response_body_len = 15;
    std::strcpy(ctx.response_content_type, "application/json");

    auto output = coalescing_leader_completion_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "completed-l1-ready");

    // Verify it's completed in registry (subsequent request within window is Follower)
    auto now = std::chrono::system_clock::now();
    std::uint64_t now_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());

    auto res =
        coalescing::registry_register(&registry, ctx.coalescing_decision.key, now_ms, 100, 5);
    EXPECT_EQ(res.role, coalescing::InFlightRole::Follower);
}

TEST_F(CoalescingLeaderCompletionTest, NonCacheableLeaderClears) {
    ctx.response_status_code = 500; // not cacheable

    coalescing::registry_register(&registry, ctx.coalescing_decision.key, 1000, 100, 5);

    auto output = coalescing_leader_completion_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "completed-cleared");

    // Verify it's cleared (subsequent request is Leader again)
    auto now = std::chrono::system_clock::now();
    std::uint64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    auto res =
        coalescing::registry_register(&registry, ctx.coalescing_decision.key, now_ms, 100, 5);
    EXPECT_EQ(res.role, coalescing::InFlightRole::Leader);
}

} // namespace bytetaper::stages

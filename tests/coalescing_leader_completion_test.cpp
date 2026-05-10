// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/coalescing_completion_handoff.h"
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

TEST_F(CoalescingLeaderCompletionTest, L2HandoffOnLargeBody) {
    auto register_res =
        coalescing::registry_register(&registry, ctx.coalescing_decision.key, 1000, 100, 5);
    ctx.coalescing_decision.lifecycle_generation = register_res.lifecycle_generation;

    ctx.cache_key_ready = true;
    ctx.response_body = "large-body-placeholder";
    ctx.response_body_len = 3073; // > kL1MaxBodySize (3072 bytes)
    std::strcpy(ctx.response_content_type, "application/json");

    auto output = coalescing_leader_completion_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "awaiting-l2-completion");

    // The entry should STILL be in-flight in the registry (not L1Ready)
    coalescing::RegistrySharedResponseOutput shared{};
    auto wait_res = coalescing::registry_wait_for_completion(
        &registry, ctx.coalescing_decision.key, 0, ctx.coalescing_decision.lifecycle_generation,
        &shared);
    // Since wait-budget is 0, it should timeout (it's InFlight, not Completed)
    EXPECT_EQ(wait_res, coalescing::RegistryWaitResult::Timeout);
}

TEST_F(CoalescingLeaderCompletionTest, GenerationalSafety) {
    auto register_res =
        coalescing::registry_register(&registry, ctx.coalescing_decision.key, 1000, 100, 5);
    ctx.coalescing_decision.lifecycle_generation = register_res.lifecycle_generation;

    ctx.cache_key_ready = true;
    ctx.response_body = "{\"status\":\"ok\"}";
    ctx.response_body_len = 15;
    std::strcpy(ctx.response_content_type, "application/json");

    // Try to complete with a stale/incorrect generation (non-zero)
    ctx.coalescing_decision.lifecycle_generation = register_res.lifecycle_generation + 1;

    auto output = coalescing_leader_completion_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);

    coalescing::RegistrySharedResponseOutput shared{};
    auto wait_res = coalescing::registry_wait_for_completion(
        &registry, ctx.coalescing_decision.key, 0, register_res.lifecycle_generation, &shared);
    EXPECT_EQ(wait_res, coalescing::RegistryWaitResult::Timeout); // Still InFlight
}

TEST(CoalescingCompletionHandoffDecisionTest, HandoffClassificationTiers) {
    using namespace coalescing;

    // - body_len == 0 -> NotCacheable
    EXPECT_EQ(decide_coalescing_completion_handoff(0),
              CoalescingCompletionHandoffTarget::NotCacheable);

    // - body_len == kL1MaxBodySize -> L1Inline
    EXPECT_EQ(decide_coalescing_completion_handoff(cache::kL1MaxBodySize),
              CoalescingCompletionHandoffTarget::L1Inline);

    // - body_len == kL1MaxBodySize + 1 -> L2Completion
    EXPECT_EQ(decide_coalescing_completion_handoff(cache::kL1MaxBodySize + 1),
              CoalescingCompletionHandoffTarget::L2Completion);

    // - body_len == kL2BodyBufSize -> L2Completion
    EXPECT_EQ(decide_coalescing_completion_handoff(apg::ApgTransformContext::kL2BodyBufSize),
              CoalescingCompletionHandoffTarget::L2Completion);

    // - body_len == kL2BodyBufSize + 1 -> StoreToL2NoFollowerHandoff
    EXPECT_EQ(decide_coalescing_completion_handoff(apg::ApgTransformContext::kL2BodyBufSize + 1),
              CoalescingCompletionHandoffTarget::StoreToL2NoFollowerHandoff);

    // - body_len == kL2MaxBodySize -> StoreToL2NoFollowerHandoff
    EXPECT_EQ(decide_coalescing_completion_handoff(cache::kL2MaxBodySize),
              CoalescingCompletionHandoffTarget::StoreToL2NoFollowerHandoff);

    // - body_len == kL2MaxBodySize + 1 -> TooLargeForL2
    EXPECT_EQ(decide_coalescing_completion_handoff(cache::kL2MaxBodySize + 1),
              CoalescingCompletionHandoffTarget::TooLargeForL2);
}

TEST_F(CoalescingLeaderCompletionTest, StoreToL2NoFollowerHandoffStageTransition) {
    auto register_res =
        coalescing::registry_register(&registry, ctx.coalescing_decision.key, 1000, 100, 5);
    ctx.coalescing_decision.lifecycle_generation = register_res.lifecycle_generation;

    ctx.cache_key_ready = true;
    ctx.response_body = "large-body-no-handoff";
    ctx.response_body_len =
        apg::ApgTransformContext::kL2BodyBufSize + 100; // between 64KiB and 1MiB
    std::strcpy(ctx.response_content_type, "application/json");

    auto output = coalescing_leader_completion_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "awaiting-l2-completion-no-handoff");

    // Registry state should have transitioned to TooLargeForHandoff
    coalescing::RegistrySharedResponseOutput shared{};
    auto wait_res = coalescing::registry_wait_for_completion(
        &registry, ctx.coalescing_decision.key, 0, ctx.coalescing_decision.lifecycle_generation,
        &shared);
    EXPECT_EQ(wait_res, coalescing::RegistryWaitResult::TooLargeForHandoff);
}

TEST_F(CoalescingLeaderCompletionTest, TooLargeForL2StageTransition) {
    auto register_res =
        coalescing::registry_register(&registry, ctx.coalescing_decision.key, 1000, 100, 5);
    ctx.coalescing_decision.lifecycle_generation = register_res.lifecycle_generation;

    ctx.cache_key_ready = true;
    ctx.response_body = "body-larger-than-l2-limit";
    ctx.response_body_len = cache::kL2MaxBodySize + 1; // > 1MiB
    std::strcpy(ctx.response_content_type, "application/json");

    auto output = coalescing_leader_completion_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "completed-not-cacheable");

    // Registry state should have transitioned to NotCacheable
    coalescing::RegistrySharedResponseOutput shared{};
    auto wait_res = coalescing::registry_wait_for_completion(
        &registry, ctx.coalescing_decision.key, 0, ctx.coalescing_decision.lifecycle_generation,
        &shared);
    EXPECT_EQ(wait_res, coalescing::RegistryWaitResult::NotCacheable);
}

} // namespace bytetaper::stages

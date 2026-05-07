// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "coalescing/coalescing_decision.h"

#include <gtest/gtest.h>

namespace bytetaper::coalescing {

class CoalescingDecisionTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_init(&registry);

        policy.enabled = true;
        policy.wait_window_ms = 100;
        policy.max_waiters_per_key = 2;

        ctx.policy = &policy;
        ctx.method = policy::HttpMethod::Get;
        ctx.safety_input.allow_authenticated = false;
        ctx.safety_input.has_authorization_header = false;
        ctx.safety_input.has_cookie_header = false;
        ctx.key_input.normalized_path = "/api/v1/resource";
        ctx.key_input.method = policy::HttpMethod::Get;
        ctx.key_input.route_name = "test-route";
        ctx.now_ms = 1000;
    }

    InFlightRegistry registry;
    policy::CoalescingPolicy policy;
    CoalescingDecisionContext ctx;
};

TEST_F(CoalescingDecisionTest, PolicyDisabledBypasses) {
    policy.enabled = false;
    auto decision = evaluate_coalescing_decision(&registry, ctx);
    EXPECT_EQ(decision.action, CoalescingAction::Bypass);
    EXPECT_EQ(decision.reason, CoalescingDecisionReason::PolicyDisabled);
    EXPECT_STREQ(get_decision_reason_string(decision.reason).data(), "policy_disabled");
}

TEST_F(CoalescingDecisionTest, NonGetBypasses) {
    ctx.method = policy::HttpMethod::Post;
    auto decision = evaluate_coalescing_decision(&registry, ctx);
    EXPECT_EQ(decision.action, CoalescingAction::Bypass);
    EXPECT_EQ(decision.reason, CoalescingDecisionReason::MethodNotGet);
}

TEST_F(CoalescingDecisionTest, AuthenticatedBypasses) {
    ctx.safety_input.has_authorization_header = true;
    auto decision = evaluate_coalescing_decision(&registry, ctx);
    EXPECT_EQ(decision.action, CoalescingAction::Bypass);
    EXPECT_EQ(decision.reason, CoalescingDecisionReason::AuthenticatedRequest);
}

TEST_F(CoalescingDecisionTest, MissingKeyBypasses) {
    ctx.key_input.normalized_path = nullptr; // build_coalescing_key fails if path is null
    auto decision = evaluate_coalescing_decision(&registry, ctx);
    EXPECT_EQ(decision.action, CoalescingAction::Bypass);
    EXPECT_EQ(decision.reason, CoalescingDecisionReason::MissingKey);
}

TEST_F(CoalescingDecisionTest, LeaderFollowerFlow) {
    // 1. Leader
    auto d1 = evaluate_coalescing_decision(&registry, ctx);
    EXPECT_EQ(d1.action, CoalescingAction::Leader);
    EXPECT_EQ(d1.reason, CoalescingDecisionReason::LeaderCreated);
    EXPECT_STRNE(d1.key, "");

    // 2. Follower
    auto d2 = evaluate_coalescing_decision(&registry, ctx);
    EXPECT_EQ(d2.action, CoalescingAction::Follower);
    EXPECT_EQ(d2.reason, CoalescingDecisionReason::FollowerJoined);
    EXPECT_STREQ(d1.key, d2.key);
}

TEST_F(CoalescingDecisionTest, TooManyWaitersRejects) {
    evaluate_coalescing_decision(&registry, ctx); // leader
    evaluate_coalescing_decision(&registry, ctx); // follower 1
    evaluate_coalescing_decision(&registry, ctx); // follower 2 (limit reached)

    // 4. Reject
    auto d3 = evaluate_coalescing_decision(&registry, ctx);
    EXPECT_EQ(d3.action, CoalescingAction::Reject);
    EXPECT_EQ(d3.reason, CoalescingDecisionReason::TooManyWaiters);
    EXPECT_STREQ(get_decision_reason_string(d3.reason).data(), "too_many_waiters");
}

} // namespace bytetaper::coalescing

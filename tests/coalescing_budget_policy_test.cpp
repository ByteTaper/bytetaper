// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/coalescing_policy.h"
#include "policy/coalescing_policy_validator.h"

#include <gtest/gtest.h>

namespace bytetaper::policy {

TEST(CoalescingBudgetPolicyTest, DefaultValuesArePerfect) {
    CoalescingPolicy p{};
    EXPECT_EQ(p.backend_timeout_ms, 500);
    EXPECT_EQ(p.handoff_buffer_ms, 250);
    EXPECT_EQ(p.result_ready_retention_ms, 50);
}

TEST(CoalescingBudgetPolicyTest, ValidBudgetsAreAccepted) {
    CoalescingPolicy p{};
    p.enabled = true;
    p.backend_timeout_ms = 200;
    p.handoff_buffer_ms = 50;
    p.result_ready_retention_ms = 20;
    p.max_waiters_per_key = 10;
    p.require_cache_enabled = false;

    EXPECT_EQ(validate_coalescing_policy(p), nullptr);
    EXPECT_EQ(validate_coalescing_policy_safe(p), nullptr);
}

TEST(CoalescingBudgetPolicyTest, RejectsInvalidBackendTimeout) {
    CoalescingPolicy p{};
    p.enabled = true;
    p.handoff_buffer_ms = 10;
    p.result_ready_retention_ms = 10;
    p.max_waiters_per_key = 10;

    p.backend_timeout_ms = 0;
    EXPECT_STREQ(validate_coalescing_policy(p), "backend_timeout_ms must be > 0");

    p.backend_timeout_ms = 35000;
    EXPECT_STREQ(validate_coalescing_policy(p), "backend_timeout_ms must be <= 30000");
}

TEST(CoalescingBudgetPolicyTest, RejectsInvalidHandoffBuffer) {
    CoalescingPolicy p{};
    p.enabled = true;
    p.backend_timeout_ms = 100;
    p.result_ready_retention_ms = 10;
    p.max_waiters_per_key = 10;

    p.handoff_buffer_ms = 0;
    EXPECT_STREQ(validate_coalescing_policy(p), "handoff_buffer_ms must be > 0");

    p.handoff_buffer_ms = 6000;
    EXPECT_STREQ(validate_coalescing_policy(p), "handoff_buffer_ms must be <= 5000");
}

TEST(CoalescingBudgetPolicyTest, RejectsInvalidRetentionWindow) {
    CoalescingPolicy p{};
    p.enabled = true;
    p.backend_timeout_ms = 100;
    p.handoff_buffer_ms = 50;
    p.max_waiters_per_key = 10;

    p.result_ready_retention_ms = 5;
    EXPECT_STREQ(validate_coalescing_policy(p), "result_ready_retention_ms must be >= 10");

    p.result_ready_retention_ms = 1500;
    EXPECT_STREQ(validate_coalescing_policy(p), "result_ready_retention_ms must be <= 1000");

    p.result_ready_retention_ms = 120;
    EXPECT_STREQ(validate_coalescing_policy(p),
                 "result_ready_retention_ms must be < backend_timeout_ms");
}

TEST(CoalescingBudgetPolicyTest, RejectsBudgetOverflow) {
    CoalescingPolicy p{};
    p.enabled = true;
    p.backend_timeout_ms = 28000;
    p.handoff_buffer_ms = 3000;
    p.result_ready_retention_ms = 50;
    p.max_waiters_per_key = 10;
    p.require_cache_enabled = false;

    // Total = 31000ms which exceeds kMaxWaitWindowMs (30000)
    EXPECT_STREQ(validate_coalescing_policy_safe(p),
                 "coalescing total wait budget exceeds 30000ms");
}

TEST(CoalescingBudgetPolicyTest, RejectsUnhealthyRatio) {
    CoalescingPolicy p{};
    p.enabled = true;
    p.backend_timeout_ms = 100;
    p.handoff_buffer_ms = 150; // Buffer exceeds upstream timeout
    p.result_ready_retention_ms = 10;
    p.max_waiters_per_key = 10;
    p.require_cache_enabled = false;

    EXPECT_STREQ(validate_coalescing_policy_safe(p),
                 "coalescing handoff_buffer_ms exceeds backend_timeout_ms");
}

} // namespace bytetaper::policy

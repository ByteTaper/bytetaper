// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/cache_policy.h"
#include "policy/coalescing_policy_validator.h"

#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <unistd.h>

namespace bytetaper::policy {

class CoalescingPolicyValidatorTest : public ::testing::Test {
protected:
    CoalescingPolicy policy{};
    CachePolicy cache_policy{};

    void SetUp() override {
        policy.enabled = true;
        policy.backend_timeout_ms = 500;
        policy.handoff_buffer_ms = 250;
        policy.result_ready_retention_ms = 50;
        policy.max_waiters_per_key = 64;
        policy.require_cache_enabled = false;
        policy.allow_authenticated = false;

        cache_policy.enabled = false;
    }
};

TEST_F(CoalescingPolicyValidatorTest, ValidMinimal) {
    EXPECT_EQ(validate_coalescing_policy_safe(policy, &cache_policy), nullptr);
}

TEST_F(CoalescingPolicyValidatorTest, DisabledPolicyIsAlwaysValid) {
    policy.enabled = false;
    policy.backend_timeout_ms = 0; // structurally invalid but policy is disabled
    EXPECT_EQ(validate_coalescing_policy_safe(policy, &cache_policy), nullptr);
}

TEST_F(CoalescingPolicyValidatorTest, RejectInvalidWaitWindow) {
    policy.backend_timeout_ms = 0;
    EXPECT_STREQ(validate_coalescing_policy_safe(policy, &cache_policy),
                 "backend_timeout_ms must be > 0");

    policy.backend_timeout_ms = 100;
    policy.result_ready_retention_ms = 100;
    EXPECT_STREQ(validate_coalescing_policy_safe(policy, &cache_policy),
                 "result_ready_retention_ms must be < backend_timeout_ms");
}

TEST_F(CoalescingPolicyValidatorTest, RejectInvalidMaxWaiters) {
    policy.max_waiters_per_key = 0;
    EXPECT_STREQ(validate_coalescing_policy_safe(policy, &cache_policy),
                 "coalescing.max_waiters_per_key must be > 0");
}

TEST_F(CoalescingPolicyValidatorTest, RequireCacheDependency) {
    policy.require_cache_enabled = true;
    cache_policy.enabled = false;
    EXPECT_STREQ(validate_coalescing_policy_safe(policy, &cache_policy),
                 "coalescing requires cache to be enabled");

    cache_policy.enabled = true;
    EXPECT_EQ(validate_coalescing_policy_safe(policy, &cache_policy), nullptr);
}

TEST_F(CoalescingPolicyValidatorTest, RequireAuthScopeForAuthenticated) {
    policy.allow_authenticated = true;
    cache_policy.auth_scope_header[0] = '\0';
    EXPECT_STREQ(validate_coalescing_policy_safe(policy, &cache_policy),
                 "coalescing.allow_authenticated=true requires cache.auth_scope_header to be set");

    std::strcpy(cache_policy.auth_scope_header, "x-user-id");
    EXPECT_EQ(validate_coalescing_policy_safe(policy, &cache_policy), nullptr);
}

TEST_F(CoalescingPolicyValidatorTest, RejectNullCachePolicyIfRequired) {
    policy.require_cache_enabled = true;
    EXPECT_STREQ(validate_coalescing_policy_safe(policy, nullptr),
                 "coalescing requires cache to be enabled");
}

TEST_F(CoalescingPolicyValidatorTest, PrintsEffectiveFollowerWaitBudgetToStderr) {
    // Save original stderr
    int original_stderr = dup(STDERR_FILENO);

    // Create a temporary file in workspace to capture stderr
    char temp_path_workspace[] = "bytetaper_test_stderr_XXXXXX";
    int temp_fd = mkstemp(temp_path_workspace);
    ASSERT_NE(temp_fd, -1);

    // Redirect stderr to our temporary file
    dup2(temp_fd, STDERR_FILENO);
    close(temp_fd);

    // Run the validator which prints to stderr
    validate_coalescing_policy_safe(policy, &cache_policy);

    // Flush stderr
    fflush(stderr);

    // Restore stderr
    dup2(original_stderr, STDERR_FILENO);
    close(original_stderr);

    // Read the contents of the temporary file
    std::FILE* f = std::fopen(temp_path_workspace, "r");
    char buf[256] = {};
    if (f != nullptr) {
        std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        std::fclose(f);
    }

    // Clean up
    std::remove(temp_path_workspace);

    // Assert the content contains the printed follower wait budget
    EXPECT_TRUE(std::strstr(buf, "coalescing.follower_wait_budget_ms = 500 + 250 = 750") != nullptr)
        << "Actual stderr content: " << buf;
}

} // namespace bytetaper::policy

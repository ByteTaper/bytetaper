// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "hash/hash.h"
#include "metrics/coalescing_metrics.h"
#include "policy/coalescing_policy_validator.h"
#include "policy/yaml_loader.h"
#include "stages/coalescing_follower_wait_stage.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

class FollowerWaitGuardrailTest : public ::testing::Test {
protected:
    void SetUp() override {
        l1_cache = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1_cache.get());

        coalescing_metrics = std::make_unique<metrics::CoalescingMetrics>();
        ctx.coalescing_metrics = coalescing_metrics.get();

        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.route_id = "12345";

        ctx.matched_policy = &policy;
        ctx.l1_cache = l1_cache.get();
        std::strcpy(ctx.raw_path, "/path");
        ctx.request_method = policy::HttpMethod::Get;

        policy.coalescing.enabled = true;
        policy.coalescing.backend_timeout_ms = 40;
        policy.coalescing.handoff_buffer_ms = 10;
        policy.coalescing.result_ready_retention_ms = 10;

        auto now = std::chrono::system_clock::now();
        ctx.request_epoch_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        registry = std::make_unique<coalescing::InFlightRegistry>();
        coalescing::registry_init(registry.get());

        ctx.coalescing_decision.action = coalescing::CoalescingAction::Follower;
        std::strcpy(ctx.coalescing_decision.key, "c_key:test:1:/path");
        ctx.coalescing_registry = registry.get();
    }

    std::unique_ptr<coalescing::InFlightRegistry> registry;
    std::unique_ptr<cache::L1Cache> l1_cache;
    policy::RoutePolicy policy;
    apg::ApgTransformContext ctx;
    std::unique_ptr<metrics::CoalescingMetrics> coalescing_metrics;
};

TEST_F(FollowerWaitGuardrailTest, BudgetCapFiresAndBypasses) {
    // Wait budget is backend_timeout_ms (40) + handoff_buffer_ms (10) = 50ms.
    // Set guardrail to 20ms, which is lower than wait budget.
    policy.coalescing.max_follower_wait_budget_ms = 20;

    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    ASSERT_NE(output.note, nullptr);
    EXPECT_STREQ(output.note, "guardrail-budget-exceeded");

    EXPECT_EQ(coalescing_metrics->follower_guardrail_budget_exceeded_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->fallback_total.load(), 1u);
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Bypass);
    EXPECT_EQ(coalescing::registry_active_waiters(registry.get()), 0u);
}

TEST_F(FollowerWaitGuardrailTest, GlobalWaiterCapFiresAndBypasses) {
    policy.coalescing.max_active_follower_waiters = 3;

    // Simulate 3 waiters already active globally
    registry->active_waiters.store(3, std::memory_order_relaxed);

    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    ASSERT_NE(output.note, nullptr);
    EXPECT_STREQ(output.note, "guardrail-global-limit");

    EXPECT_EQ(coalescing_metrics->follower_guardrail_global_limit_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->fallback_total.load(), 1u);
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Bypass);
}

TEST_F(FollowerWaitGuardrailTest, ShardWaiterCapFiresAndBypasses) {
    policy.coalescing.max_active_follower_waiters_per_shard = 2;

    // Determine the shard index for our key
    std::uint64_t hash = bytetaper::hash::hash_cstr_runtime(ctx.coalescing_decision.key);
    std::size_t shard_idx = hash % coalescing::kInFlightShards;

    // Simulate 2 waiters already active in this specific shard
    registry->shards[shard_idx].active_waiters.store(2, std::memory_order_relaxed);

    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    ASSERT_NE(output.note, nullptr);
    EXPECT_STREQ(output.note, "guardrail-shard-limit");

    EXPECT_EQ(coalescing_metrics->follower_guardrail_shard_limit_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->fallback_total.load(), 1u);
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Bypass);
}

TEST_F(FollowerWaitGuardrailTest, GuardrailsDisabledByDefault) {
    policy.coalescing.max_follower_wait_budget_ms = 0;
    policy.coalescing.max_active_follower_waiters = 0;
    policy.coalescing.max_active_follower_waiters_per_shard = 0;

    // Without registering a leader, stage should proceed and return "no-registry"
    // or block and time out if leader is registered.
    coalescing::registry_register(registry.get(), ctx.coalescing_decision.key, ctx.request_epoch_ms,
                                  50, 128);

    auto output = coalescing_follower_wait_stage(ctx);
    // Should proceed to wait and time out (since no response is stored)
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    ASSERT_NE(output.note, nullptr);
    EXPECT_STREQ(output.note, "timeout-fallback");

    EXPECT_EQ(coalescing_metrics->follower_guardrail_budget_exceeded_total.load(), 0u);
    EXPECT_EQ(coalescing_metrics->follower_guardrail_global_limit_total.load(), 0u);
    EXPECT_EQ(coalescing_metrics->follower_guardrail_shard_limit_total.load(), 0u);
}

TEST(FollowerWaitGuardrailYamlTest, YamlParserDefaultsAndLoading) {
    const char* yaml_str = R"(
routes:
  - id: "test-route"
    match: { kind: "prefix", prefix: "/test" }
    coalescing:
      enabled: true
      backend_timeout_ms: 400
      handoff_buffer_ms: 100
      max_waiters_per_key: 10
      max_follower_wait_budget_ms: 500
      max_active_follower_waiters: 10
      max_active_follower_waiters_per_shard: 5
)";

    policy::PolicyFileResult result{};
    bool ok = policy::load_policy_from_string(yaml_str, &result);
    if (!ok || !result.ok) {
        std::fprintf(stderr, "YAML Load Error: %s\n", result.error ? result.error : "unknown");
    }
    ASSERT_TRUE(ok);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.count, 1u);

    const auto& coal = result.policies[0].coalescing;
    EXPECT_TRUE(coal.enabled);
    EXPECT_EQ(coal.backend_timeout_ms, 400u);
    EXPECT_EQ(coal.handoff_buffer_ms, 100u);
    EXPECT_EQ(coal.max_waiters_per_key, 10u);
    EXPECT_EQ(coal.max_follower_wait_budget_ms, 500u);
    EXPECT_EQ(coal.max_active_follower_waiters, 10u);
    EXPECT_EQ(coal.max_active_follower_waiters_per_shard, 5u);
}

TEST(FollowerWaitGuardrailYamlTest, YamlParserDefaultsWhenFieldsOmitted) {
    const char* yaml_str = R"(
routes:
  - id: "test-route"
    match: { kind: "prefix", prefix: "/test" }
    coalescing:
      enabled: true
      backend_timeout_ms: 400
      handoff_buffer_ms: 100
      max_waiters_per_key: 10
)";

    policy::PolicyFileResult result{};
    bool ok = policy::load_policy_from_string(yaml_str, &result);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.count, 1u);

    const auto& coal = result.policies[0].coalescing;
    EXPECT_TRUE(coal.enabled);
    EXPECT_EQ(coal.backend_timeout_ms, 400u);
    EXPECT_EQ(coal.handoff_buffer_ms, 100u);
    EXPECT_EQ(coal.max_waiters_per_key, 10u);
    // Asserting the omission of new guardrail fields defaults to 0
    EXPECT_EQ(coal.max_follower_wait_budget_ms, 0u);
    EXPECT_EQ(coal.max_active_follower_waiters, 0u);
    EXPECT_EQ(coal.max_active_follower_waiters_per_shard, 0u);
}

TEST(FollowerWaitGuardrailValidatorTest, ValidatorRules) {
    policy::CachePolicy cp{};
    cp.enabled = true;

    policy::RoutePolicy p{};
    p.coalescing.enabled = true;
    p.coalescing.backend_timeout_ms = 400;
    p.coalescing.handoff_buffer_ms = 100;
    p.coalescing.max_waiters_per_key = 10;
    p.coalescing.result_ready_retention_ms = 50;
    p.coalescing.require_cache_enabled = true;

    // Valid configuration
    p.coalescing.max_follower_wait_budget_ms = 200;
    p.coalescing.max_active_follower_waiters = 10;
    p.coalescing.max_active_follower_waiters_per_shard = 5;
    EXPECT_EQ(policy::validate_coalescing_policy_safe(p.coalescing, &cp), nullptr);

    // Error: max_follower_wait_budget_ms < handoff_buffer_ms
    p.coalescing.max_follower_wait_budget_ms = 50; // less than handoff_buffer_ms (100)
    EXPECT_STREQ(policy::validate_coalescing_policy_safe(p.coalescing, &cp),
                 "coalescing max_follower_wait_budget_ms must be >= handoff_buffer_ms");

    // Restore budget
    p.coalescing.max_follower_wait_budget_ms = 200;

    // Error: max_active_follower_waiters_per_shard > max_active_follower_waiters
    p.coalescing.max_active_follower_waiters_per_shard = 15; // exceeds global limit (10)
    EXPECT_STREQ(
        policy::validate_coalescing_policy_safe(p.coalescing, &cp),
        "coalescing max_active_follower_waiters_per_shard must be <= max_active_follower_waiters");
}

} // namespace bytetaper::stages

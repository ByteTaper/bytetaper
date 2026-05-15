// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/inflight_registry.h"
#include "metrics/coalescing_metrics.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_follower_wait_stage.h"

#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

class CoalescingFollowerWaitTest : public ::testing::Test {
protected:
    void SetUp() override {
        l1_cache = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1_cache.get());

        coalescing_metrics = std::make_unique<metrics::CoalescingMetrics>();
        ctx.coalescing_metrics = coalescing_metrics.get();

        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.route_id = "12345";

        store_.count = 0;
        runtime::route_cache_epoch_register(&store_, policy.route_id);

        ctx.matched_policy = &policy;
        ctx.l1_cache = l1_cache.get();
        ctx.route_cache_epoch_store = &store_;
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
    runtime::RouteCacheEpochStore store_;
    policy::RoutePolicy policy;
    apg::ApgTransformContext ctx;
    std::unique_ptr<metrics::CoalescingMetrics> coalescing_metrics;
};

TEST_F(CoalescingFollowerWaitTest, NonFollowerBypasses) {
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Leader;
    auto output = coalescing_follower_wait_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
}

TEST_F(CoalescingFollowerWaitTest, ImmediateHitReturnsSkip) {
    // Populate cache first
    cache::CacheEntry entry;
    std::strcpy(entry.content_type, "application/json");
    entry.body = "{\"data\": \"cached\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = ctx.request_epoch_ms;
    entry.expires_at_epoch_ms = ctx.request_epoch_ms + 60000;

    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;

    cache::build_cache_key(ki, entry.key, sizeof(entry.key));

    cache::l1_put(l1_cache.get(), entry);

    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);
    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);
    EXPECT_TRUE(ctx.cache_hit);
    EXPECT_STREQ(ctx.cache_layer, "L1");
}

TEST_F(CoalescingFollowerWaitTest, TimeoutReturnsContinue) {
    // Register leader so follower actually enters wait
    coalescing::registry_register(registry.get(), ctx.coalescing_decision.key, ctx.request_epoch_ms,
                                  50, 128);

    policy.coalescing.backend_timeout_ms = 15; // Short wait
    policy.coalescing.handoff_buffer_ms = 5;

    auto start = std::chrono::steady_clock::now();
    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(output.result, apg::StageResult::Continue);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(duration, 20);
}

TEST_F(CoalescingFollowerWaitTest, L2SeededNoL1HitTimesOut) {
    // Register leader
    coalescing::registry_register(registry.get(), ctx.coalescing_decision.key, ctx.request_epoch_ms,
                                  50, 128);

    policy.coalescing.backend_timeout_ms = 15; // short timeout
    policy.coalescing.handoff_buffer_ms = 5;

    // No L1 population, so L1 will always miss.
    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_FALSE(ctx.cache_hit);
}

TEST_F(CoalescingFollowerWaitTest, FollowerDirectWait_L1Ready) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    // Populate L1 cache
    cache::CacheEntry entry;
    std::strcpy(entry.content_type, "application/json");
    entry.body = "{\"data\": \"direct-wait-cached\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = ctx.request_epoch_ms;
    entry.expires_at_epoch_ms = ctx.request_epoch_ms + 60000;

    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;

    cache::build_cache_key(ki, entry.key, sizeof(entry.key));
    cache::l1_put(l1_cache.get(), entry);

    // Complete registry with L1Ready
    coalescing::registry_complete_state_if_generation(
        registry.get(), ctx.coalescing_decision.key, ctx.coalescing_decision.lifecycle_generation,
        coalescing::InFlightCompletionState::L1Ready, ctx.request_epoch_ms);

    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);
    EXPECT_TRUE(ctx.cache_hit);
    EXPECT_STREQ(ctx.cache_layer, "L1");
}

TEST_F(CoalescingFollowerWaitTest, FollowerDirectWait_Timeout) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    policy.coalescing.backend_timeout_ms = 10;
    policy.coalescing.handoff_buffer_ms = 5;

    auto start = std::chrono::steady_clock::now();
    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "timeout-fallback");

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(duration, 15);
    EXPECT_LT(duration, 100); // Does not exceed budget by a significant margin
}

TEST_F(CoalescingFollowerWaitTest, FollowerDirectWait_L2ReadyButMiss) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    // Initialize temporary empty L2 cache
    const char* db_path = "/tmp/bytetaper_follower_wait_l2_miss_test";
    cache::l2_destroy(db_path);
    auto* l2 = cache::l2_open(db_path);
    ASSERT_NE(l2, nullptr);
    ctx.l2_cache = l2;

    // Complete registry with L2Ready
    coalescing::registry_complete_state_if_generation(
        registry.get(), ctx.coalescing_decision.key, ctx.coalescing_decision.lifecycle_generation,
        coalescing::InFlightCompletionState::L2Ready, ctx.request_epoch_ms);

    cache_key_prepare_stage(ctx);
    ctx.cache_key_ready = true;
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "l2-ready-but-miss-fallback");

    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_miss_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_l2ready_total.load(), 1u);

    // Cleanup
    cache::l2_close(&l2);
    cache::l2_destroy(db_path);
}

TEST_F(CoalescingFollowerWaitTest, FollowerDirectWait_L2ReadyButNoL2Cache) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    // Complete registry with L2Ready
    coalescing::registry_complete_state_if_generation(
        registry.get(), ctx.coalescing_decision.key, ctx.coalescing_decision.lifecycle_generation,
        coalescing::InFlightCompletionState::L2Ready, ctx.request_epoch_ms);

    cache_key_prepare_stage(ctx);
    ctx.l2_cache = nullptr; // Explicitly ensure l2_cache is null to fail precondition check
    ctx.cache_key_ready = true;
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "l2-ready-but-miss-fallback");

    // Precondition check should prevent any probe metric increments
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_total.load(), 0u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_miss_total.load(), 0u);
}

TEST_F(CoalescingFollowerWaitTest, FollowerDirectWait_L2Ready) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    // Initialize temporary L2 cache
    const char* db_path = "/tmp/bytetaper_follower_wait_l2_test";
    cache::l2_destroy(db_path);
    auto* l2 = cache::l2_open(db_path);
    ASSERT_NE(l2, nullptr);
    ctx.l2_cache = l2;

    // Populate L2 cache
    cache::CacheEntry entry;
    std::strcpy(entry.content_type, "application/json");
    entry.body = "{\"data\": \"l2-direct-wait-cached\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = ctx.request_epoch_ms;
    entry.expires_at_epoch_ms = ctx.request_epoch_ms + 60000;

    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;

    cache::build_cache_key(ki, entry.key, sizeof(entry.key));

    ASSERT_TRUE(cache::l2_put(l2, entry));

    // Complete registry with L2Ready
    coalescing::registry_complete_state_if_generation(
        registry.get(), ctx.coalescing_decision.key, ctx.coalescing_decision.lifecycle_generation,
        coalescing::InFlightCompletionState::L2Ready, ctx.request_epoch_ms);

    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);
    EXPECT_TRUE(ctx.cache_hit);
    EXPECT_STREQ(ctx.cache_layer, "L2");

    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_hit_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_l2ready_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_latency_ms_count.load(), 1u);

    // Cleanup
    cache::l2_close(&l2);
    cache::l2_destroy(db_path);
}

TEST_F(CoalescingFollowerWaitTest, TimeoutFinalProbe_L2Hit) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    cache_key_prepare_stage(ctx);
    ctx.cache_key_ready = true;

    const char* db_path = "/tmp/bytetaper_final_probe_l2_test";
    cache::l2_destroy(db_path);
    auto* l2 = cache::l2_open(db_path);
    ASSERT_NE(l2, nullptr);
    ctx.l2_cache = l2;

    cache::CacheEntry entry{};
    std::strcpy(entry.key, ctx.cache_key);
    std::strcpy(entry.content_type, "application/json");
    entry.body = "{\"val\": \"l2-final-probe\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = ctx.request_epoch_ms;
    entry.expires_at_epoch_ms = ctx.request_epoch_ms + 10000;
    ASSERT_TRUE(cache::l2_put(l2, entry));

    policy.coalescing.backend_timeout_ms = 0;
    policy.coalescing.handoff_buffer_ms = 0;

    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);
    EXPECT_TRUE(ctx.cache_hit);
    EXPECT_STREQ(ctx.cache_layer, "L2");

    EXPECT_EQ(coalescing_metrics->fallback_total.load(), 0u);
    EXPECT_GE(coalescing_metrics->follower_cache_hit_total.load(), 1u);
    EXPECT_GE(coalescing_metrics->follower_l2_hit_total.load(), 1u);

    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_hit_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_timeout_final_total.load(), 1u);

    cache::l2_close(&l2);
    cache::l2_destroy(db_path);
}

TEST_F(CoalescingFollowerWaitTest, TimeoutFinalProbe_BothMiss) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    // Initialize temporary empty L2 cache
    const char* db_path = "/tmp/bytetaper_timeout_final_miss_l2_test";
    cache::l2_destroy(db_path);
    auto* l2 = cache::l2_open(db_path);
    ASSERT_NE(l2, nullptr);
    ctx.l2_cache = l2;

    cache_key_prepare_stage(ctx);
    ctx.cache_key_ready = true;

    policy.coalescing.backend_timeout_ms = 0;
    policy.coalescing.handoff_buffer_ms = 0;

    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "timeout-fallback");
    EXPECT_FALSE(ctx.cache_hit);

    EXPECT_GE(coalescing_metrics->fallback_total.load(), 1u);
    EXPECT_GE(coalescing_metrics->follower_timeout_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_cache_hit_total.load(), 0u);

    // Sync L2 probe metrics assertions
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_miss_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_timeout_final_total.load(), 1u);

    // Cleanup
    cache::l2_close(&l2);
    cache::l2_destroy(db_path);
}

TEST_F(CoalescingFollowerWaitTest, TimeoutFinalProbe_L2Unavailable) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    cache_key_prepare_stage(ctx);
    ctx.l2_cache = nullptr;

    policy.coalescing.backend_timeout_ms = 0;
    policy.coalescing.handoff_buffer_ms = 0;

    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "timeout-fallback");
    EXPECT_FALSE(ctx.cache_hit);

    EXPECT_GE(coalescing_metrics->fallback_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_cache_hit_total.load(), 0u);
}

TEST_F(CoalescingFollowerWaitTest, FollowerTooLargeForHandoffTransition) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    // Complete registry with TooLargeForHandoff state
    coalescing::registry_complete_state_if_generation(
        registry.get(), ctx.coalescing_decision.key, ctx.coalescing_decision.lifecycle_generation,
        coalescing::InFlightCompletionState::TooLargeForHandoff, ctx.request_epoch_ms);

    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "too-large-for-handoff-fallback");
    EXPECT_FALSE(ctx.cache_hit);

    EXPECT_EQ(coalescing_metrics->follower_too_large_for_handoff_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->fallback_total.load(), 1u);
}

TEST_F(CoalescingFollowerWaitTest, TimeoutFinalProbe_L2BodyTooLarge) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    cache_key_prepare_stage(ctx);
    ctx.cache_key_ready = true;

    const char* db_path = "/tmp/bytetaper_final_probe_l2_too_large_test";
    cache::l2_destroy(db_path);
    auto* l2 = cache::l2_open(db_path);
    ASSERT_NE(l2, nullptr);
    ctx.l2_cache = l2;

    // Put a large entry into L2 cache (size > 64 KiB, e.g. 64 KiB + 100 bytes)
    cache::CacheEntry entry{};
    std::strcpy(entry.key, ctx.cache_key);
    std::strcpy(entry.content_type, "application/json");

    std::size_t large_len = apg::ApgTransformContext::kL2BodyBufSize + 100;
    std::string large_body(large_len, 'x');
    entry.body = large_body.c_str();
    entry.body_len = large_len;
    entry.created_at_epoch_ms = ctx.request_epoch_ms;
    entry.expires_at_epoch_ms = ctx.request_epoch_ms + 10000;
    ASSERT_TRUE(cache::l2_put(l2, entry));

    policy.coalescing.backend_timeout_ms = 0;
    policy.coalescing.handoff_buffer_ms = 0;

    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "timeout-fallback");
    EXPECT_FALSE(ctx.cache_hit);

    EXPECT_EQ(coalescing_metrics->follower_timeout_l2_body_too_large_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->fallback_total.load(), 1u);

    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_body_too_large_total.load(), 1u);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_timeout_final_total.load(), 1u);

    cache::l2_close(&l2);
    cache::l2_destroy(db_path);
}

TEST_F(CoalescingFollowerWaitTest, ProbeMetrics_ProbeLatencyRecorded) {
    auto reg_res = coalescing::registry_register(registry.get(), ctx.coalescing_decision.key,
                                                 ctx.request_epoch_ms, 50, 128);
    ctx.coalescing_decision.lifecycle_generation = reg_res.lifecycle_generation;

    // Initialize temporary L2 cache
    const char* db_path = "/tmp/bytetaper_follower_wait_l2_latency_test";
    cache::l2_destroy(db_path);
    auto* l2 = cache::l2_open(db_path);
    ASSERT_NE(l2, nullptr);
    ctx.l2_cache = l2;

    // Populate L2 cache
    cache::CacheEntry entry;
    std::strcpy(entry.content_type, "application/json");
    entry.body = "{\"data\": \"l2-direct-wait-cached\"}";
    entry.body_len = std::strlen(entry.body);
    entry.created_at_epoch_ms = ctx.request_epoch_ms;
    entry.expires_at_epoch_ms = ctx.request_epoch_ms + 60000;

    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.route_id = policy.route_id;
    ki.path = ctx.raw_path;
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;

    cache::build_cache_key(ki, entry.key, sizeof(entry.key));

    ASSERT_TRUE(cache::l2_put(l2, entry));

    // Complete registry with L2Ready
    coalescing::registry_complete_state_if_generation(
        registry.get(), ctx.coalescing_decision.key, ctx.coalescing_decision.lifecycle_generation,
        coalescing::InFlightCompletionState::L2Ready, ctx.request_epoch_ms);

    cache_key_prepare_stage(ctx);
    auto output = coalescing_follower_wait_stage(ctx);

    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);
    EXPECT_EQ(coalescing_metrics->follower_sync_l2_probe_latency_ms_count.load(), 1u);

    // Cleanup
    cache::l2_close(&l2);
    cache::l2_destroy(db_path);
}

} // namespace bytetaper::stages

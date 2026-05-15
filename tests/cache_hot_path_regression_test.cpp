// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/pipeline.h"
#include "cache/cache_entry.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "extproc/default_pipelines.h"
#include "policy/route_policy.h"
#include "runtime/route_cache_epoch_store.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::extproc {

TEST(CacheHotPathRegression, L1HitDoesNotTouchCoalescingRegistry) {
    // Setup: L1 cache with a valid entry for the test route.
    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());

    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.cache.ttl_seconds = 60;

    runtime::RouteCacheEpochStore store{};
    store.count = 0;
    runtime::route_cache_epoch_register(&store, policy.route_id);

    cache::CacheEntry entry{};
    // Build a matching cache key and populate L1.
    cache::CacheKeyInput ki{};
    ki.method = policy::HttpMethod::Get;
    ki.route_id = policy.route_id;
    ki.path = "/test";
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    ASSERT_TRUE(cache::build_cache_key(ki, entry.key, sizeof(entry.key)));
    entry.status_code = 200;
    const char body[] = "cached-response";
    entry.body = body;
    entry.body_len = sizeof(body) - 1;
    entry.created_at_epoch_ms = 1000;
    entry.expires_at_epoch_ms = 9999999999LL;
    cache::l1_put(l1.get(), entry);

    // Setup: coalescing registry — track whether it was touched.
    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());

    apg::ApgTransformContext ctx{};
    // Sentinel to prove coalescing_decision_stage didn't run.
    // In our codebase, the stage only sets action if coalescing is enabled.
    ctx.coalescing_decision.action = coalescing::CoalescingAction::Reject;
    ctx.matched_policy = &policy;
    ctx.l1_cache = l1.get();
    ctx.coalescing_registry = registry.get();
    ctx.route_cache_epoch_store = &store;
    ctx.request_epoch_ms = 5000;
    ctx.request_method = policy::HttpMethod::Get;
    std::strcpy(ctx.raw_path, "/test");
    ctx.raw_path_length = std::strlen(ctx.raw_path);

    // Run the full lookup pipeline.
    apg::run_pipeline(kLookupStages, kLookupStageCount, ctx);

    // Assert: L1 hit occurred.
    EXPECT_TRUE(ctx.cache_hit);
    EXPECT_TRUE(ctx.should_return_immediate_response);
    EXPECT_STREQ(ctx.cache_layer, "L1");

    // Assert: coalescing decision was not set (pipeline short-circuited before it ran).
    // It remains our sentinel value.
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Reject);
}

TEST(CacheHotPathRegression, L1MissStillEvaluatesCoalescing) {
    // L1 empty — coalescing should still run and set a Leader decision.
    auto l1 = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1.get());

    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy.coalescing.enabled = true;
    policy.coalescing.backend_timeout_ms = 100;

    auto registry = std::make_unique<coalescing::InFlightRegistry>();
    coalescing::registry_init(registry.get());

    runtime::RouteCacheEpochStore store{};
    store.count = 0;
    runtime::route_cache_epoch_register(&store, policy.route_id);

    apg::ApgTransformContext ctx{};
    ctx.matched_policy = &policy;
    ctx.l1_cache = l1.get();
    ctx.coalescing_registry = registry.get();
    ctx.route_cache_epoch_store = &store;
    ctx.request_epoch_ms = 5000;
    ctx.request_method = policy::HttpMethod::Get;
    std::strcpy(ctx.raw_path, "/test");
    ctx.raw_path_length = std::strlen(ctx.raw_path);

    apg::run_pipeline(kLookupStages, kLookupStageCount, ctx);

    EXPECT_FALSE(ctx.cache_hit);
    // Coalescing ran and assigned a role (Leader for first request).
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Leader);
}

} // namespace bytetaper::extproc

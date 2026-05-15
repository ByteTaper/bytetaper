// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/pipeline.h"
#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "coalescing/inflight_registry.h"
#include "extproc/default_pipelines.h"
#include "policy/route_policy.h"
#include "runtime/route_cache_epoch_store.h"
#include "runtime/worker_queue.h"
#include "stages/cache_key_prepare_stage.h"
#include "stages/coalescing_decision_stage.h"
#include "stages/coalescing_follower_wait_stage.h"
#include "stages/compression_decision_stage.h"
#include "stages/field_variant_admission_stage.h"
#include "stages/l1_cache_lookup_stage.h"
#include "stages/l1_variant_lookup_stage.h"
#include "stages/l2_cache_async_lookup_enqueue_stage.h"
#include "stages/mutation_invalidation_apply_stage.h"
#include "stages/mutation_invalidation_prepare_stage.h"
#include "stages/pagination_request_mutation_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::extproc {

class DefaultPipelineOrderTest : public ::testing::Test {
protected:
    void SetUp() override {
        l1 = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1.get());

        registry = std::make_unique<coalescing::InFlightRegistry>();
        coalescing::registry_init(registry.get());

        q = std::make_unique<runtime::WorkerQueue>();
        runtime::WorkerQueueConfig cfg;
        cfg.worker_count = 1;
        runtime::worker_queue_init(q.get(), cfg);
        q->running = true;

        policy.route_id = "test-route";
        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.coalescing.enabled = true;

        store.count = 0;
        runtime::route_cache_epoch_register(&store, policy.route_id);

        ctx.matched_policy = &policy;
        ctx.l1_cache = l1.get();
        ctx.coalescing_registry = registry.get();
        ctx.worker_queue = q.get();
        ctx.route_cache_epoch_store = &store;
        ctx.coalescing_decision.action = coalescing::CoalescingAction::Bypass;

        std::strcpy(ctx.raw_path, "/test");
        ctx.request_method = policy::HttpMethod::Get;
    }

    void TearDown() override {
        runtime::worker_queue_shutdown(q.get());
    }

    std::unique_ptr<cache::L1Cache> l1;
    std::unique_ptr<coalescing::InFlightRegistry> registry;
    std::unique_ptr<runtime::WorkerQueue> q;
    runtime::RouteCacheEpochStore store;
    policy::RoutePolicy policy;
    apg::ApgTransformContext ctx;
};

TEST_F(DefaultPipelineOrderTest, L1HitBypassesCoalescingRegistry) {
    // 1. Populate L1
    cache::CacheEntry entry{};
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.path = ctx.raw_path;
    ki.route_id = policy.route_id;
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    cache::build_cache_key(ki, entry.key, sizeof(entry.key));

    entry.expires_at_epoch_ms = 2000000000000LL;
    cache::l1_put(l1.get(), entry);

    // 2. Run pipeline
    auto output = apg::run_pipeline(kLookupStages, kLookupStageCount, ctx);

    // 3. Assert SkipRemaining (L1 hit)
    EXPECT_EQ(output.result, apg::StageResult::SkipRemaining);

    // 4. Assert registry is untouched (no leader/follower action)
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Bypass);
}

TEST_F(DefaultPipelineOrderTest, L1HitDoesNotEnqueueL2AsyncLookup) {
    // 1. Populate L1
    cache::CacheEntry entry{};
    cache::CacheKeyInput ki{};
    ki.method = ctx.request_method;
    ki.path = ctx.raw_path;
    ki.route_id = policy.route_id;
    ki.policy_version = policy.route_id;
    ki.route_cache_epoch = 1;
    ki.route_cache_epoch_ready = true;
    cache::build_cache_key(ki, entry.key, sizeof(entry.key));

    entry.expires_at_epoch_ms = 2000000000000LL;
    cache::l1_put(l1.get(), entry);

    // 2. Run pipeline
    apg::run_pipeline(kLookupStages, kLookupStageCount, ctx);

    // 3. Assert worker queue is empty
    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(q->shards[i].lookup_count, 0u);
        EXPECT_EQ(q->shards[i].store_count, 0u);
    }
}

TEST_F(DefaultPipelineOrderTest, L1MissEvaluatesCoalescing) {
    // 1. Empty L1 (default)

    // 2. Run pipeline
    apg::run_pipeline(kLookupStages, kLookupStageCount, ctx);

    // 3. Assert coalescing decision was made
    // (It should be Leader because registry was empty)
    EXPECT_EQ(ctx.coalescing_decision.action, coalescing::CoalescingAction::Leader);
}

TEST_F(DefaultPipelineOrderTest, DefaultPipelineContainsNoSyncRocksDB) {
    // This is a static/runtime check of the stage pointers to enforce the documented phase order.
    ASSERT_EQ(kLookupStageCount, 9u);
    EXPECT_EQ(kLookupStages[0], stages::mutation_invalidation_prepare_stage);
    EXPECT_EQ(kLookupStages[1], stages::cache_key_prepare_stage);
    EXPECT_EQ(kLookupStages[2], stages::field_variant_admission_stage);
    EXPECT_EQ(kLookupStages[3], stages::l1_cache_lookup_stage);
    EXPECT_EQ(kLookupStages[4], stages::l1_variant_lookup_stage);
    EXPECT_EQ(kLookupStages[5], stages::coalescing_decision_stage);
    EXPECT_EQ(kLookupStages[6], stages::coalescing_follower_wait_stage);
    EXPECT_EQ(kLookupStages[7], stages::l2_cache_async_lookup_enqueue_stage);
    EXPECT_EQ(kLookupStages[8], stages::pagination_request_mutation_stage);

    ASSERT_EQ(kResponseStageCount, 2u);
    EXPECT_EQ(kResponseStages[0], stages::mutation_invalidation_apply_stage);
    EXPECT_EQ(kResponseStages[1], stages::compression_decision_stage);
}

} // namespace bytetaper::extproc

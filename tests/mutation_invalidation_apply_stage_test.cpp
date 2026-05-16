// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "runtime/route_cache_epoch_store.h"
#include "stages/mutation_invalidation_apply_stage.h"

#include <gtest/gtest.h>

using namespace bytetaper;

class MutationInvalidationApplyStageTest : public ::testing::Test {
protected:
    apg::ApgTransformContext context{};
    runtime::RouteCacheEpochStore store{};
    metrics::CacheMetrics metrics{};
    policy::RoutePolicy active_routes[2]{};

    void SetUp() override {
        context = apg::ApgTransformContext{};
        context.route_cache_epoch_store = &store;
        context.cache_metrics = &metrics;

        context.active_routes = active_routes;
        context.active_route_count = 0;
    }
};

TEST_F(MutationInvalidationApplyStageTest, SkipsWhenNoPlan) {
    context.mutation_invalidation.prepared = false;
    auto output = stages::mutation_invalidation_apply_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision, apg::MutationInvalidationDecision::None);
    EXPECT_EQ(metrics.invalidation_apply_attempt_total.load(), 0u);
}

TEST_F(MutationInvalidationApplyStageTest, SkipsWhenStatusNotSuccessful) {
    context.mutation_invalidation.prepared = true;
    context.mutation_invalidation.success_status_min = 200;
    context.mutation_invalidation.success_status_max = 299;
    context.response_status_code = 500;

    auto output = stages::mutation_invalidation_apply_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedStatusNotSuccessful);
    EXPECT_EQ(metrics.invalidation_apply_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_skipped_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_status_not_successful_total.load(), 1u);
}

TEST_F(MutationInvalidationApplyStageTest, AppliesWhenStatusIs204) {
    context.mutation_invalidation.prepared = true;
    context.mutation_invalidation.success_status_min = 200;
    context.mutation_invalidation.success_status_max = 299;
    context.response_status_code = 204;
    context.mutation_invalidation.target_count = 1;
    std::strcpy(context.mutation_invalidation.targets[0].route_id, "target-route");

    runtime::route_cache_epoch_register(&store, "target-route");

    std::uint64_t before = 0;
    runtime::route_cache_epoch_get(&store, "target-route", &before);

    auto output = stages::mutation_invalidation_apply_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision, apg::MutationInvalidationDecision::Applied);

    std::uint64_t after = 0;
    runtime::route_cache_epoch_get(&store, "target-route", &after);
    EXPECT_EQ(after, before + 1);

    EXPECT_EQ(metrics.invalidation_apply_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_applied_total.load(), 1u);
    EXPECT_EQ(metrics.route_epoch_bump_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.route_epoch_bump_success_total.load(), 1u);
}

TEST_F(MutationInvalidationApplyStageTest, DoesNotApplyTwice) {
    context.mutation_invalidation.prepared = true;
    context.mutation_invalidation.success_status_min = 200;
    context.mutation_invalidation.success_status_max = 299;
    context.response_status_code = 200;
    context.mutation_invalidation.target_count = 1;
    std::strcpy(context.mutation_invalidation.targets[0].route_id, "r1");
    runtime::route_cache_epoch_register(&store, "r1");

    stages::mutation_invalidation_apply_stage(context);
    EXPECT_TRUE(context.mutation_invalidation.applied);

    std::uint64_t mid = 0;
    runtime::route_cache_epoch_get(&store, "r1", &mid);

    stages::mutation_invalidation_apply_stage(context);
    std::uint64_t after = 0;
    runtime::route_cache_epoch_get(&store, "r1", &after);
    EXPECT_EQ(after, mid); // should not have incremented again
}

TEST_F(MutationInvalidationApplyStageTest, FailsWhenEpochStoreMissing) {
    context.mutation_invalidation.prepared = true;
    context.mutation_invalidation.success_status_min = 200;
    context.mutation_invalidation.success_status_max = 299;
    context.response_status_code = 200;
    context.route_cache_epoch_store = nullptr;

    stages::mutation_invalidation_apply_stage(context);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::FailedEpochStoreMissing);
    EXPECT_TRUE(context.mutation_invalidation.failed);
    EXPECT_EQ(metrics.invalidation_apply_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_failed_total.load(), 1u);
}

TEST_F(MutationInvalidationApplyStageTest, FailsWhenTargetRouteMissing) {
    context.mutation_invalidation.prepared = true;
    context.mutation_invalidation.success_status_min = 200;
    context.mutation_invalidation.success_status_max = 299;
    context.response_status_code = 200;
    context.mutation_invalidation.target_count = 1;
    std::strcpy(context.mutation_invalidation.targets[0].route_id, "non-existent");

    stages::mutation_invalidation_apply_stage(context);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::FailedEpochBump);
    EXPECT_TRUE(context.mutation_invalidation.failed);
    EXPECT_EQ(metrics.invalidation_apply_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.route_epoch_bump_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.route_epoch_bump_failed_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_failed_total.load(), 1u);
}

TEST_F(MutationInvalidationApplyStageTest, AppliesMultipleTargets) {
    context.mutation_invalidation.prepared = true;
    context.mutation_invalidation.success_status_min = 200;
    context.mutation_invalidation.success_status_max = 299;
    context.response_status_code = 200;
    context.mutation_invalidation.target_count = 2;
    std::strcpy(context.mutation_invalidation.targets[0].route_id, "r1");
    std::strcpy(context.mutation_invalidation.targets[1].route_id, "r2");

    runtime::route_cache_epoch_register(&store, "r1");
    runtime::route_cache_epoch_register(&store, "r2");

    std::uint64_t b1 = 0, b2 = 0;
    runtime::route_cache_epoch_get(&store, "r1", &b1);
    runtime::route_cache_epoch_get(&store, "r2", &b2);

    auto output = stages::mutation_invalidation_apply_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision, apg::MutationInvalidationDecision::Applied);
    EXPECT_TRUE(context.mutation_invalidation.applied);
    EXPECT_STREQ(context.mutation_invalidation.reason, "epoch-bumped");

    std::uint64_t a1 = 0, a2 = 0;
    runtime::route_cache_epoch_get(&store, "r1", &a1);
    runtime::route_cache_epoch_get(&store, "r2", &a2);
    EXPECT_EQ(a1, b1 + 1);
    EXPECT_EQ(a2, b2 + 1);

    EXPECT_EQ(metrics.invalidation_apply_attempt_total.load(), 1u);
    EXPECT_EQ(metrics.invalidation_applied_total.load(), 1u);
    EXPECT_EQ(metrics.route_epoch_bump_attempt_total.load(), 2u);
    EXPECT_EQ(metrics.route_epoch_bump_success_total.load(), 2u);
}

TEST_F(MutationInvalidationApplyStageTest, VariantsTracked) {
    context.mutation_invalidation.prepared = true;
    context.mutation_invalidation.success_status_min = 200;
    context.mutation_invalidation.success_status_max = 299;
    context.response_status_code = 200;
    context.mutation_invalidation.target_count = 1;
    std::strcpy(context.mutation_invalidation.targets[0].route_id, "v1");

    runtime::route_cache_epoch_register(&store, "v1");

    // Setup active route with variants enabled
    active_routes[0].route_id = "v1";
    active_routes[0].cache.field_variant.enabled = true;
    context.active_route_count = 1;

    stages::mutation_invalidation_apply_stage(context);
    EXPECT_EQ(metrics.variant_invalidated_by_route_epoch_total.load(), 1u);
}

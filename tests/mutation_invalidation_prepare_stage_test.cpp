// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "policy/route_policy.h"
#include "stages/mutation_invalidation_prepare_stage.h"

#include <gtest/gtest.h>

using namespace bytetaper;

class MutationInvalidationPrepareStageTest : public ::testing::Test {
protected:
    apg::ApgTransformContext context{};
    policy::RoutePolicy policy{};
    policy::RoutePolicy targets[4]{};

    void SetUp() override {
        context = apg::ApgTransformContext{};
        policy = policy::RoutePolicy{};
        policy.route_id = "mutator";
        context.matched_policy = &policy;

        // Setup active routes for resolver
        static const char* target_ids[] = { "t1", "t2", "t3", "t4" };
        for (int i = 0; i < 4; ++i) {
            targets[i].route_id = target_ids[i];
            targets[i].allowed_method = policy::HttpMethod::Get;
            targets[i].cache.enabled = true;
            targets[i].cache.behavior = policy::CacheBehavior::Store;
        }
        context.active_routes = targets;
        context.active_route_count = 4;
    }
};

TEST_F(MutationInvalidationPrepareStageTest, SkipsWhenNoPolicy) {
    context.matched_policy = nullptr;
    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedNoPolicy);
}

TEST_F(MutationInvalidationPrepareStageTest, SkipsWhenMethodIsGet) {
    context.request_method = policy::HttpMethod::Get;
    policy.cache.invalidation.enabled = true;
    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedNonMutationMethod);
}

TEST_F(MutationInvalidationPrepareStageTest, SkipsWhenInvalidationDisabled) {
    context.request_method = policy::HttpMethod::Post;
    policy.cache.invalidation.enabled = false;
    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "no-invalidation-policy");
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedNoPolicy);
}

TEST_F(MutationInvalidationPrepareStageTest, SkipsWhenMethodIsPost) {
    context.request_method = policy::HttpMethod::Post;
    policy.cache.invalidation.enabled = true;
    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_STREQ(output.note, "non-mutation-method");
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedNonMutationMethod);
}

TEST_F(MutationInvalidationPrepareStageTest, PreparesForPatch) {
    context.request_method = policy::HttpMethod::Patch;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_patch = true;
    policy.cache.invalidation.target_count = 1;
    std::strcpy(policy.cache.invalidation.targets[0].route_id, "t1");

    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision, apg::MutationInvalidationDecision::Prepared);
    EXPECT_STREQ(context.mutation_invalidation.targets[0].route_id, "t1");
}

TEST_F(MutationInvalidationPrepareStageTest, PreparesForDelete) {
    context.request_method = policy::HttpMethod::Delete;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_delete = true;
    policy.cache.invalidation.target_count = 1;
    std::strcpy(policy.cache.invalidation.targets[0].route_id, "t2");

    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision, apg::MutationInvalidationDecision::Prepared);
    EXPECT_STREQ(context.mutation_invalidation.targets[0].route_id, "t2");
}

TEST_F(MutationInvalidationPrepareStageTest, PreparesForPut) {
    context.request_method = policy::HttpMethod::Put;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_put = true;
    policy.cache.invalidation.target_count = 1;
    std::strcpy(policy.cache.invalidation.targets[0].route_id, "t3");
    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(context.mutation_invalidation.decision, apg::MutationInvalidationDecision::Prepared);
}

TEST_F(MutationInvalidationPrepareStageTest, SkipsWhenMethodNotEnabled) {
    context.request_method = policy::HttpMethod::Put;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_put = false;
    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedMethodNotEnabled);
}

TEST_F(MutationInvalidationPrepareStageTest, CopiesSuccessStatusRange) {
    context.request_method = policy::HttpMethod::Delete;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_delete = true;
    policy.cache.invalidation.success_status_min = 200;
    policy.cache.invalidation.success_status_max = 204;
    policy.cache.invalidation.target_count = 1;
    std::strcpy(policy.cache.invalidation.targets[0].route_id, "t1");

    stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(context.mutation_invalidation.success_status_min, 200);
    EXPECT_EQ(context.mutation_invalidation.success_status_max, 204);
}

TEST_F(MutationInvalidationPrepareStageTest, MultipleTargets) {
    context.request_method = policy::HttpMethod::Patch;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_patch = true;
    policy.cache.invalidation.target_count = 2;
    std::strcpy(policy.cache.invalidation.targets[0].route_id, "t1");
    std::strcpy(policy.cache.invalidation.targets[1].route_id, "t2");

    stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(context.mutation_invalidation.target_count, 2);
    EXPECT_STREQ(context.mutation_invalidation.targets[0].route_id, "t1");
    EXPECT_STREQ(context.mutation_invalidation.targets[1].route_id, "t2");
}

TEST_F(MutationInvalidationPrepareStageTest, RejectsTooManyTargetsStrictly) {
    context.request_method = policy::HttpMethod::Patch;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_patch = true;

    // Deliberately exceed fixed capacity in policy
    policy.cache.invalidation.target_count = apg::kMaxPendingInvalidationTargets + 1;

    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(output.result, apg::StageResult::Continue);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedNoPolicy);
    EXPECT_STREQ(context.mutation_invalidation.reason, "too-many-targets");
}
TEST_F(MutationInvalidationPrepareStageTest, RejectsTargetWithDisabledCache) {
    context.request_method = policy::HttpMethod::Patch;
    policy.cache.invalidation.enabled = true;
    policy.cache.invalidation.on_patch = true;
    policy.cache.invalidation.target_count = 1;

    // t4 is valid by default in SetUp, let's disable it
    targets[3].cache.enabled = false;
    std::strcpy(policy.cache.invalidation.targets[0].route_id, "t4");

    auto output = stages::mutation_invalidation_prepare_stage(context);
    EXPECT_EQ(context.mutation_invalidation.decision,
              apg::MutationInvalidationDecision::SkippedNoPolicy);
    EXPECT_STREQ(context.mutation_invalidation.reason, "target-cache-disabled");
}

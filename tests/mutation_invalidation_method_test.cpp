// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"
#include "policy/route_policy.h"
#include "stages/mutation_invalidation_prepare_stage.h"

#include <gtest/gtest.h>

using namespace bytetaper::apg;
using namespace bytetaper::stages;
using namespace bytetaper;

namespace {

class MutationInvalidationMethodTest : public ::testing::Test {
protected:
    policy::RoutePolicy target;
    policy::RoutePolicy mutation;
    policy::RoutePolicy routes[2];
    ApgTransformContext context{};

    void SetUp() override {
        target = policy::RoutePolicy{};
        target.route_id = "target";
        target.allowed_method = policy::HttpMethod::Get;
        target.cache.enabled = true;
        target.cache.behavior = policy::CacheBehavior::Store;

        mutation = policy::RoutePolicy{};
        mutation.route_id = "mutation";
        mutation.cache.invalidation.enabled = true;
        mutation.cache.invalidation.target_count = 1;
        std::strcpy(mutation.cache.invalidation.targets[0].route_id, "target");
        mutation.cache.invalidation.targets[0].strategy =
            policy::CacheInvalidationStrategy::RouteEpoch;

        mutation.cache.invalidation.on_patch = true;
        mutation.cache.invalidation.on_put = true;
        mutation.cache.invalidation.on_delete = true;

        routes[0] = target;
        routes[1] = mutation;

        context = ApgTransformContext{};
        context.active_routes = routes;
        context.active_route_count = 2;
        context.matched_policy = &mutation;
    }

    void set_method_and_prepare(const char* method) {
        context.request_method = policy::parse_http_method(method, std::strlen(method));
        mutation_invalidation_prepare_stage(context);
    }
};

TEST_F(MutationInvalidationMethodTest, ParsesPatch) {
    set_method_and_prepare("PATCH");
    EXPECT_TRUE(context.mutation_invalidation.prepared);
}

TEST_F(MutationInvalidationMethodTest, ParsesPut) {
    set_method_and_prepare("PUT");
    EXPECT_TRUE(context.mutation_invalidation.prepared);
}

TEST_F(MutationInvalidationMethodTest, ParsesDelete) {
    set_method_and_prepare("DELETE");
    EXPECT_TRUE(context.mutation_invalidation.prepared);
}

TEST_F(MutationInvalidationMethodTest, ParsesLowercasePatch) {
    set_method_and_prepare("patch");
    EXPECT_TRUE(context.mutation_invalidation.prepared);
}

TEST_F(MutationInvalidationMethodTest, DoesNotTreatPostAsMutationInvalidationV1) {
    set_method_and_prepare("POST");
    EXPECT_FALSE(context.mutation_invalidation.prepared);
}

TEST_F(MutationInvalidationMethodTest, GetDoesNotPrepareInvalidation) {
    set_method_and_prepare("GET");
    EXPECT_FALSE(context.mutation_invalidation.prepared);
}

TEST_F(MutationInvalidationMethodTest, UnknownMethodDoesNotPrepare) {
    set_method_and_prepare("OPTIONS");
    EXPECT_FALSE(context.mutation_invalidation.prepared);
}

} // namespace

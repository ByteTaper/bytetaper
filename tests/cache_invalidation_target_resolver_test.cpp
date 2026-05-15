// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_invalidation_target_resolver.h"
#include "policy/route_policy.h"

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

using namespace bytetaper;
using namespace bytetaper::cache;

class CacheInvalidationTargetResolverTest : public ::testing::Test {
protected:
    std::vector<policy::RoutePolicy> routes;

    void SetUp() override {
        routes.clear();
    }

    policy::RoutePolicy& add_route(const char* id,
                                   policy::HttpMethod method = policy::HttpMethod::Get) {
        policy::RoutePolicy r{};
        r.route_id = id;
        r.allowed_method = method;
        r.cache.enabled = true;
        r.cache.behavior = policy::CacheBehavior::Store;
        routes.push_back(r);
        return routes.back();
    }
};

TEST_F(CacheInvalidationTargetResolverTest, ResolvesSingleRouteEpochTarget) {
    auto& target = add_route("get_user", policy::HttpMethod::Get);
    auto& mutator = add_route("update_user", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "get_user");
    mutator.cache.invalidation.targets[0].strategy = policy::CacheInvalidationStrategy::RouteEpoch;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::Resolved);
    EXPECT_EQ(result.target_count, 1u);
    EXPECT_STREQ(result.targets[0].route_id, "get_user");
}

TEST_F(CacheInvalidationTargetResolverTest, ResolvesMultipleRouteEpochTargets) {
    add_route("r1");
    add_route("r2");
    auto& mutator = add_route("mut", policy::HttpMethod::Delete);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_delete = true;
    mutator.cache.invalidation.target_count = 2;
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "r1");
    mutator.cache.invalidation.targets[0].strategy = policy::CacheInvalidationStrategy::RouteEpoch;
    std::strcpy(mutator.cache.invalidation.targets[1].route_id, "r2");
    mutator.cache.invalidation.targets[1].strategy = policy::CacheInvalidationStrategy::RouteEpoch;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Delete);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.target_count, 2u);
    EXPECT_STREQ(result.targets[0].route_id, "r1");
    EXPECT_STREQ(result.targets[1].route_id, "r2");
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsNullRoutes) {
    policy::RoutePolicy mutator{};
    auto result = resolve_invalidation_targets(nullptr, 0, &mutator, policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::InvalidArgument);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsNullMutationRoute) {
    add_route("r1");
    auto result = resolve_invalidation_targets(routes.data(), routes.size(), nullptr,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::InvalidArgument);
}

TEST_F(CacheInvalidationTargetResolverTest, SkipsWhenInvalidationDisabled) {
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = false;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::Disabled);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsGetRequestMethod) {
    auto& mutator = add_route("mut", policy::HttpMethod::Get);
    mutator.cache.invalidation.enabled = true;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Get);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::NonMutationMethod);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsMethodNotEnabledByPolicy) {
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = false;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::MethodNotEnabled);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsNoTargets) {
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 0;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::NoTargets);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsTooManyTargets) {
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = kMaxResolvedInvalidationTargets + 1;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::TooManyTargets);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsUnknownTargetRoute) {
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "ghost");

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::UnknownTargetRoute);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsSelfTarget) {
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "mut");

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::SelfTarget);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsTargetThatIsNotGet) {
    add_route("target_post", policy::HttpMethod::Post);
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "target_post");

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::TargetNotGet);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsTargetWithCacheDisabled) {
    auto& target = add_route("target_disabled");
    target.cache.enabled = false;

    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "target_disabled");

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::TargetCacheDisabled);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsTargetWithNonStoreBehavior) {
    auto& target_bypass = add_route("target_bypass");
    target_bypass.cache.behavior = policy::CacheBehavior::Bypass;

    auto& target_default = add_route("target_default");
    target_default.cache.behavior = policy::CacheBehavior::Default;

    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;

    // Test Bypass
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "target_bypass");
    auto result1 = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                                policy::HttpMethod::Patch);
    EXPECT_FALSE(result1.ok);
    EXPECT_EQ(result1.status, InvalidationTargetResolveStatus::TargetCacheNotStore);

    // Test Default
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "target_default");
    auto result2 = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                                policy::HttpMethod::Patch);
    EXPECT_FALSE(result2.ok);
    EXPECT_EQ(result2.status, InvalidationTargetResolveStatus::TargetCacheNotStore);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsUnsupportedStrategy) {
    add_route("r1");
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;
    std::strcpy(mutator.cache.invalidation.targets[0].route_id, "r1");
    mutator.cache.invalidation.targets[0].strategy = policy::CacheInvalidationStrategy::ExactKey;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::UnsupportedStrategy);
}
TEST_F(CacheInvalidationTargetResolverTest, RejectsPostRequestMethodForV1) {
    auto& mutator = add_route("mut", policy::HttpMethod::Post);
    mutator.cache.invalidation.enabled = true;

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Post);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::NonMutationMethod);
}

TEST_F(CacheInvalidationTargetResolverTest, RejectsEmptyTargetRouteId) {
    auto& mutator = add_route("mut", policy::HttpMethod::Patch);
    mutator.cache.invalidation.enabled = true;
    mutator.cache.invalidation.on_patch = true;
    mutator.cache.invalidation.target_count = 1;
    // target_count 1, but targets[0].route_id is empty by default

    auto result = resolve_invalidation_targets(routes.data(), routes.size(), &mutator,
                                               policy::HttpMethod::Patch);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, InvalidationTargetResolveStatus::InvalidArgument);
    EXPECT_STREQ(result.reason, "empty-target-route-id");
}

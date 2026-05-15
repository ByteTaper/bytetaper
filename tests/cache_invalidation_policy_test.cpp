// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/cache_policy.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::policy {

TEST(CacheInvalidationPolicyTest, ValidConfiguration) {
    CachePolicy p{};
    p.enabled = true;
    p.ttl_seconds = 60;
    p.l1.enabled = true;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.success_status_min = 200;
    p.invalidation.success_status_max = 299;
    p.invalidation.target_count = 1;
    std::strncpy(p.invalidation.targets[0].route_id, "get_route", 63);
    p.invalidation.targets[0].strategy = CacheInvalidationStrategy::RouteEpoch;

    EXPECT_EQ(validate_cache_policy(p), nullptr);
}

TEST(CacheInvalidationPolicyTest, ValidWithoutCacheEnabled) {
    CachePolicy p{};
    p.enabled = false;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.success_status_min = 200;
    p.invalidation.success_status_max = 299;
    p.invalidation.target_count = 1;
    std::strncpy(p.invalidation.targets[0].route_id, "get_route", 63);
    p.invalidation.targets[0].strategy = CacheInvalidationStrategy::RouteEpoch;

    EXPECT_EQ(validate_cache_policy(p), nullptr);
}

TEST(CacheInvalidationPolicyTest, MissingOnMethodsInvalid) {
    CachePolicy p{};
    p.enabled = true;
    p.ttl_seconds = 60;
    p.l1.enabled = true;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = false;
    p.invalidation.on_put = false;
    p.invalidation.on_delete = false;
    p.invalidation.target_count = 1;
    std::strncpy(p.invalidation.targets[0].route_id, "get_route", 63);

    EXPECT_STREQ(validate_cache_policy(p),
                 "cache.invalidation requires at least one method in on_methods");
}

TEST(CacheInvalidationPolicyTest, MissingTargetsInvalid) {
    CachePolicy p{};
    p.enabled = true;
    p.ttl_seconds = 60;
    p.l1.enabled = true;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.target_count = 0;

    EXPECT_STREQ(validate_cache_policy(p), "cache.invalidation requires at least one target");
}

TEST(CacheInvalidationPolicyTest, TargetsExceedLimitInvalid) {
    CachePolicy p{};
    p.enabled = true;
    p.ttl_seconds = 60;
    p.l1.enabled = true;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.target_count = kMaxCacheInvalidationTargets + 1;

    EXPECT_STREQ(validate_cache_policy(p),
                 "cache.invalidation target count exceeds maximum allowed");
}

TEST(CacheInvalidationPolicyTest, StatusMinGreaterThanMaxInvalid) {
    CachePolicy p{};
    p.enabled = true;
    p.ttl_seconds = 60;
    p.l1.enabled = true;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.target_count = 1;
    std::strncpy(p.invalidation.targets[0].route_id, "get_route", 63);
    p.invalidation.success_status_min = 300;
    p.invalidation.success_status_max = 200;

    EXPECT_STREQ(validate_cache_policy(p),
                 "cache.invalidation success_status min cannot be greater than max");
}

TEST(CacheInvalidationPolicyTest, StatusMinLessThan100Invalid) {
    CachePolicy p{};
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.target_count = 1;
    std::strncpy(p.invalidation.targets[0].route_id, "get_route", 63);
    p.invalidation.success_status_min = 99;
    p.invalidation.success_status_max = 200;

    EXPECT_STREQ(validate_cache_policy(p), "cache.invalidation success_status min must be >= 100");
}

TEST(CacheInvalidationPolicyTest, StatusMaxGreaterThan599Invalid) {
    CachePolicy p{};
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.target_count = 1;
    std::strncpy(p.invalidation.targets[0].route_id, "get_route", 63);
    p.invalidation.success_status_min = 200;
    p.invalidation.success_status_max = 600;

    EXPECT_STREQ(validate_cache_policy(p), "cache.invalidation success_status max must be <= 599");
}

TEST(CacheInvalidationPolicyTest, InvalidWithCacheStore) {
    CachePolicy p{};
    p.enabled = true;
    p.behavior = CacheBehavior::Store;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.success_status_min = 200;
    p.invalidation.success_status_max = 299;
    p.invalidation.target_count = 1;
    std::strncpy(p.invalidation.targets[0].route_id, "get_route", 63);

    EXPECT_STREQ(validate_cache_policy(p),
                 "route cannot declare both cache store and cache invalidation");
}

TEST(CacheInvalidationPolicyTest, EmptyTargetRouteIdInvalid) {
    CachePolicy p{};
    p.enabled = true;
    p.ttl_seconds = 60;
    p.l1.enabled = true;
    p.invalidation.enabled = true;
    p.invalidation.on_patch = true;
    p.invalidation.target_count = 1;
    p.invalidation.targets[0].route_id[0] = '\0';

    EXPECT_STREQ(validate_cache_policy(p), "cache.invalidation target route_id must not be empty");
}

} // namespace bytetaper::policy

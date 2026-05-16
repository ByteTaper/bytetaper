// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/route_policy.h"
#include "policy/yaml_loader.h"

#include <gtest/gtest.h>

namespace bytetaper::policy {

TEST(YamlLoaderCacheTest, ValidYamlCacheStore) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      behavior: "store"
      ttl_seconds: 300
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].cache.behavior, CacheBehavior::Store);
    EXPECT_EQ(result.policies[0].cache.ttl_seconds, 300u);
}

TEST(YamlLoaderCacheTest, ValidYamlCacheBypass) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      behavior: "bypass"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].cache.behavior, CacheBehavior::Bypass);
}

TEST(YamlLoaderCacheTest, ValidYamlCacheDefault) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      behavior: "default"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].cache.behavior, CacheBehavior::Default);
}

TEST(YamlLoaderCacheTest, ValidYamlCacheAbsent) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].cache.behavior, CacheBehavior::Default);
}

TEST(YamlLoaderCacheTest, InvalidYamlBadBehavior) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      behavior: "flush"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderCacheTest, InvalidYamlBadTtl) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      ttl_seconds: "long"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderCacheTest, ValidYamlFieldVariantPolicy) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      behavior: "store"
      ttl_seconds: 300
      field_variant:
        enabled: true
        max_variants_per_route: 12
        min_field_count: 2
        max_field_count: 5
        admission_threshold: 3
        ttl_max_ms: 150000
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    const auto& fv = result.policies[0].cache.field_variant;
    EXPECT_TRUE(fv.enabled);
    EXPECT_EQ(fv.max_variants_per_route, 12u);
    EXPECT_EQ(fv.min_field_count, 2u);
    EXPECT_EQ(fv.max_field_count, 5u);
    EXPECT_EQ(fv.admission_threshold, 3u);
    EXPECT_EQ(fv.ttl_max_ms, 150000u);
}

TEST(YamlLoaderCacheTest, ValidYamlCacheVaryHeaders) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      behavior: "store"
      ttl_seconds: 300
      vary_headers:
        - "Accept-Language"
        - "X-Api-Version"
        - "accept-language"  # Duplicate case variation for dedup test
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    const auto& vh = result.policies[0].cache.vary_headers;
    EXPECT_EQ(vh.count, 2u);
    EXPECT_STREQ(vh.names[0], "accept-language");
    EXPECT_STREQ(vh.names[1], "x-api-version");
}

TEST(YamlLoaderCacheTest, InvalidYamlCacheVaryHeadersTooMany) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      behavior: "store"
      ttl_seconds: 300
      vary_headers:
        - "h1"
        - "h2"
        - "h3"
        - "h4"
        - "h5"
        - "h6"
        - "h7"
        - "h8"
        - "h9"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderCacheTest, InvalidatesCacheWithMinLt100) {
    const char* yaml_data = R"(
routes:
  - id: "update_user"
    match: { kind: "exact", prefix: "/user" }
    method: "patch"
    cache:
      invalidation:
        enabled: true
        on_methods: ["PATCH"]
        timing: "after_successful_upstream_response"
        success_status:
          min: 99
          max: 299
        targets:
          - route_id: "get_user"
            strategy: "route_epoch"
)";

    PolicyFileResult result;
    bool ok = load_policy_from_string(yaml_data, &result);
    EXPECT_FALSE(ok);
    EXPECT_STREQ(result.error, "cache.invalidation success_status min must be >= 100");
}

TEST(YamlLoaderCacheTest, InvalidatesCacheWithMaxGt599) {
    const char* yaml_data = R"(
routes:
  - id: "update_user"
    match: { kind: "exact", prefix: "/user" }
    method: "patch"
    cache:
      invalidation:
        enabled: true
        on_methods: ["PATCH"]
        timing: "after_successful_upstream_response"
        success_status:
          min: 200
          max: 600
        targets:
          - route_id: "get_user"
            strategy: "route_epoch"
)";

    PolicyFileResult result;
    bool ok = load_policy_from_string(yaml_data, &result);
    EXPECT_FALSE(ok);
    EXPECT_STREQ(result.error, "cache.invalidation success_status max must be <= 599");
}

TEST(YamlLoaderCacheTest, InvalidationTargetNotGetInvalid) {
    const char* yaml_data = R"(
routes:
  - id: "get_user"
    match: { kind: "exact", prefix: "/user" }
    method: "any"
    cache: { behavior: "store", enabled: true }
  - id: "update_user"
    match: { kind: "exact", prefix: "/user" }
    method: "patch"
    cache:
      invalidation:
        enabled: true
        on_methods: ["PATCH"]
        targets:
          - route_id: "get_user"
            strategy: "route_epoch"
)";

    PolicyFileResult result;
    bool ok = load_policy_from_string(yaml_data, &result);
    EXPECT_FALSE(ok);
    EXPECT_STREQ(result.error, "invalidation target route must be a cacheable GET route");
}

TEST(YamlLoaderCacheTest, HappyPathMutationInvalidation) {
    const char* yaml_data = R"(
routes:
  - id: "get_user"
    match: { kind: "exact", prefix: "/user" }
    method: "get"
    cache: { behavior: "store", enabled: true }
  - id: "patch_user"
    match: { kind: "exact", prefix: "/user" }
    method: "patch"
    cache:
      invalidation:
        enabled: true
        on_methods: ["PATCH"]
        targets: [{ route_id: "get_user", strategy: "route_epoch" }]
  - id: "put_user"
    match: { kind: "exact", prefix: "/user" }
    method: "put"
    cache:
      invalidation:
        enabled: true
        on_methods: ["PUT"]
        targets: [{ route_id: "get_user", strategy: "route_epoch" }]
  - id: "delete_user"
    match: { kind: "exact", prefix: "/user" }
    method: "delete"
    cache:
      invalidation:
        enabled: true
        on_methods: ["DELETE"]
        targets: [{ route_id: "get_user", strategy: "route_epoch" }]
)";

    PolicyFileResult result;
    bool ok = load_policy_from_string(yaml_data, &result);
    EXPECT_TRUE(ok) << result.error;
    EXPECT_EQ(result.policies[1].cache.invalidation.enabled, true);
    EXPECT_EQ(result.policies[1].cache.invalidation.on_patch, true);
    EXPECT_EQ(result.policies[2].cache.invalidation.on_put, true);
    EXPECT_EQ(result.policies[3].cache.invalidation.on_delete, true);
}

TEST(YamlLoaderCacheTest, RejectGetOrPostInvalidationTrigger) {
    const char* yaml_data_get = R"(
routes:
  - id: "get_user"
    match: { kind: "exact", prefix: "/user" }
    method: "get"
    cache:
      invalidation:
        enabled: true
        on_methods: ["GET"]
        targets: [{ route_id: "get_user", strategy: "route_epoch" }]
)";
    PolicyFileResult result_get;
    EXPECT_FALSE(load_policy_from_string(yaml_data_get, &result_get));
    EXPECT_STREQ(result_get.error, "cache.invalidation on_methods cannot contain GET");

    const char* yaml_data_post = R"(
routes:
  - id: "get_user"
    match: { kind: "exact", prefix: "/user" }
    method: "get"
    cache: { behavior: "store", enabled: true }
  - id: "post_user"
    match: { kind: "exact", prefix: "/user" }
    method: "post"
    cache:
      invalidation:
        enabled: true
        on_methods: ["POST"]
        targets: [{ route_id: "get_user", strategy: "route_epoch" }]
)";
    PolicyFileResult result_post;
    EXPECT_FALSE(load_policy_from_string(yaml_data_post, &result_post));
    EXPECT_STREQ(result_post.error, "cache.invalidation on_methods cannot contain POST");
}

} // namespace bytetaper::policy
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

} // namespace bytetaper::policy

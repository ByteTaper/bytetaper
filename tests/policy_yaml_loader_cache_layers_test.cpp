// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/route_policy.h"
#include "policy/yaml_loader.h"

#include <gtest/gtest.h>
#include <string>

namespace bytetaper::policy {

TEST(YamlLoaderCacheLayersTest, ParsesL1LayerEnabledAndCapacity) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l1:
          enabled: true
          capacity_entries: 42
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(result.policies[0].cache.l1.enabled);
    EXPECT_EQ(result.policies[0].cache.l1.capacity_entries, 42u);
}

TEST(YamlLoaderCacheLayersTest, ParsesL2LayerEnabledAndPath) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l2:
          enabled: true
          path: "/var/cache/bt"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(result.policies[0].cache.l2.enabled);
    EXPECT_STREQ(result.policies[0].cache.l2.path, "/var/cache/bt");
}

TEST(YamlLoaderCacheLayersTest, ParsesPrivateCacheAndAuthScopeHeader) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      private_cache: true
      auth_scope_header: "X-Auth-Scope"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(result.policies[0].cache.private_cache);
    EXPECT_STREQ(result.policies[0].cache.auth_scope_header, "x-auth-scope");
}

TEST(YamlLoaderCacheLayersTest, RejectsNonMapCacheLayers) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers: "not-a-map"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
    EXPECT_STREQ(result.error, "cache.layers must be a map");
}

TEST(YamlLoaderCacheLayersTest, RejectsNonMapL1Layer) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l1: "not-a-map"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
    EXPECT_STREQ(result.error, "cache.layers.l1 must be a map");
}

TEST(YamlLoaderCacheLayersTest, RejectsNonMapL2Layer) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l2: "not-a-map"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
    EXPECT_STREQ(result.error, "cache.layers.l2 must be a map");
}

TEST(YamlLoaderCacheLayersTest, RejectsTooLongL2Path) {
    std::string path(kMaxCachePathLen + 10, 'a');
    std::string yaml = "routes:\n  - id: \"r1\"\n    match: { kind: \"prefix\", prefix: "
                       "\"/\" }\n    cache:\n      layers:\n        l2:\n          path: \"" +
                       path + "\"\n";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml.c_str(), &result));
    EXPECT_STREQ(result.error, "cache.layers.l2.path exceeds max length");
}

TEST(YamlLoaderCacheLayersTest, RejectsEmptyAuthScopeHeader) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      auth_scope_header: ""
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
    EXPECT_STREQ(result.error, "cache.auth_scope_header must not be empty");
}

TEST(YamlLoaderCacheLayersTest, NormalizesAuthScopeHeaderToLowercase) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      auth_scope_header: "AUTHORIZATION"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_STREQ(result.policies[0].cache.auth_scope_header, "authorization");
}

TEST(YamlLoaderCacheLayersTest, PolicyIdentityChangesWhenL1EnabledChanges) {
    const char* yaml1 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l1:
          enabled: false
)";
    const char* yaml2 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l1:
          enabled: true
)";
    PolicyFileResult r1{}, r2{};
    EXPECT_TRUE(load_policy_from_string(yaml1, &r1));
    EXPECT_TRUE(load_policy_from_string(yaml2, &r2));
    EXPECT_STRNE(r1.policies[0].policy_identity, r2.policies[0].policy_identity);
}

TEST(YamlLoaderCacheLayersTest, PolicyIdentityChangesWhenL1CapacityChanges) {
    const char* yaml1 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l1:
          capacity_entries: 100
)";
    const char* yaml2 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l1:
          capacity_entries: 200
)";
    PolicyFileResult r1{}, r2{};
    EXPECT_TRUE(load_policy_from_string(yaml1, &r1));
    EXPECT_TRUE(load_policy_from_string(yaml2, &r2));
    EXPECT_STRNE(r1.policies[0].policy_identity, r2.policies[0].policy_identity);
}

TEST(YamlLoaderCacheLayersTest, PolicyIdentityChangesWhenL2PathChanges) {
    const char* yaml1 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l2:
          path: "/path1"
)";
    const char* yaml2 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l2:
          path: "/path2"
)";
    PolicyFileResult r1{}, r2{};
    EXPECT_TRUE(load_policy_from_string(yaml1, &r1));
    EXPECT_TRUE(load_policy_from_string(yaml2, &r2));
    EXPECT_STRNE(r1.policies[0].policy_identity, r2.policies[0].policy_identity);
}

TEST(YamlLoaderCacheLayersTest, PolicyIdentityChangesWhenPrivateCacheChanges) {
    const char* yaml1 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      private_cache: false
)";
    const char* yaml2 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      private_cache: true
)";
    PolicyFileResult r1{}, r2{};
    EXPECT_TRUE(load_policy_from_string(yaml1, &r1));
    EXPECT_TRUE(load_policy_from_string(yaml2, &r2));
    EXPECT_STRNE(r1.policies[0].policy_identity, r2.policies[0].policy_identity);
}

TEST(YamlLoaderCacheLayersTest, PolicyIdentityChangesWhenL2EnabledChanges) {
    const char* yaml1 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l2:
          enabled: false
)";
    const char* yaml2 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      layers:
        l2:
          enabled: true
)";
    PolicyFileResult r1{}, r2{};
    EXPECT_TRUE(load_policy_from_string(yaml1, &r1));
    EXPECT_TRUE(load_policy_from_string(yaml2, &r2));
    EXPECT_STRNE(r1.policies[0].policy_identity, r2.policies[0].policy_identity);
}

TEST(YamlLoaderCacheLayersTest, PolicyIdentityChangesWhenAuthScopeHeaderChanges) {
    const char* yaml1 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      auth_scope_header: "header1"
)";
    const char* yaml2 = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    cache:
      auth_scope_header: "header2"
)";
    PolicyFileResult r1{}, r2{};
    EXPECT_TRUE(load_policy_from_string(yaml1, &r1));
    EXPECT_TRUE(load_policy_from_string(yaml2, &r2));
    EXPECT_STRNE(r1.policies[0].policy_identity, r2.policies[0].policy_identity);
}

TEST(YamlLoaderCacheLayersTest, ExistingCachePolicyWithoutLayersStillLoads) {
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
    EXPECT_FALSE(result.policies[0].cache.l1.enabled);
    EXPECT_FALSE(result.policies[0].cache.l2.enabled);
}

} // namespace bytetaper::policy

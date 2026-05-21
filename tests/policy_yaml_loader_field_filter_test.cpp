// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/route_policy.h"
#include "policy/yaml_loader.h"

#include <gtest/gtest.h>

namespace bytetaper::policy {

TEST(YamlLoaderFieldFilterTest, ValidYamlAllowlist) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    field_filter:
      mode: "allowlist"
      fields: ["id", "name"]
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].field_filter.mode, FieldFilterMode::Allowlist);
    EXPECT_EQ(result.policies[0].field_filter.field_count, 2u);
    EXPECT_STREQ(result.policies[0].field_filter.fields[0], "id");
    EXPECT_STREQ(result.policies[0].field_filter.fields[1], "name");
}

TEST(YamlLoaderFieldFilterTest, ValidYamlDenylist) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    field_filter:
      mode: "denylist"
      fields: ["secret"]
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].field_filter.mode, FieldFilterMode::Denylist);
    EXPECT_EQ(result.policies[0].field_filter.field_count, 1u);
    EXPECT_STREQ(result.policies[0].field_filter.fields[0], "secret");
}

TEST(YamlLoaderFieldFilterTest, ValidYamlModeNone) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    field_filter:
      mode: "none"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].field_filter.mode, FieldFilterMode::None);
}

TEST(YamlLoaderFieldFilterTest, ValidYamlFieldFilterAbsent) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].field_filter.mode, FieldFilterMode::None);
}

TEST(YamlLoaderFieldFilterTest, InvalidYamlBadMode) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    field_filter:
      mode: "passthrough"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderFieldFilterTest, InvalidYamlTooManyFields) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    field_filter:
      mode: "allowlist"
      fields: ["f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12","f13","f14","f15","f16","f17"]
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderFieldFilterTest, CacheStoreWithoutLayersDefaultsL1Enabled) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "exact", prefix: "/api/v1/small" }
    mutation: "full"
    cache:
      enabled: true
      behavior: store
      ttl_seconds: 120
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(result.policies[0].cache.l1.enabled);
}

TEST(YamlLoaderFieldFilterTest, IncludeFieldsUnderFieldVariantMapsToAllowlist) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "exact", prefix: "/api/v1/small" }
    mutation: "full"
    cache:
      behavior: store
      field_variant:
        enabled: true
        include_fields: ["service", "sentinel"]
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].field_filter.mode, FieldFilterMode::Allowlist);
    EXPECT_EQ(result.policies[0].field_filter.field_count, 2u);
    EXPECT_STREQ(result.policies[0].field_filter.fields[0], "service");
    EXPECT_STREQ(result.policies[0].field_filter.fields[1], "sentinel");
}

TEST(YamlLoaderFieldFilterTest, InvalidYamlEmptyFieldName) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    field_filter:
      mode: "allowlist"
      fields: ["id", ""]
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

} // namespace bytetaper::policy

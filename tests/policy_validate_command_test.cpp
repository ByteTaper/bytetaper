// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/policy_semantic_validator.h"
#include "policy/yaml_loader.h"

#include <gtest/gtest.h>

namespace bytetaper::policy {

TEST(ValidateCommandLogicTest, ValidFileLoadsAndAllRoutesPass) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
)";
    PolicyFileResult result{};
    ASSERT_TRUE(load_policy_from_string(yaml, &result));
    ASSERT_GT(result.count, 0u);

    PolicyValidationResult validation{};
    PolicyValidationOptions options{};
    options.collect_all = false;
    options.include_warnings = true;

    EXPECT_TRUE(validate_policy_file_semantic(result, &validation, options));
    EXPECT_TRUE(validation.ok);
}

TEST(ValidateCommandLogicTest, InvalidYamlReturnsTwoPath) {
    const char* yaml = "!!! invalid yaml !!!";
    PolicyFileResult result{};
    // simulate exit code 2 path
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(ValidateCommandLogicTest, InvalidRoutePolicyReturnsThreePath) {
    const char* yaml = R"(
routes:
  - id: ""
    match: { kind: "prefix", prefix: "/" }
)";
    PolicyFileResult result{};
    ASSERT_TRUE(load_policy_from_string(yaml, &result));
    ASSERT_EQ(result.count, 1u);

    PolicyValidationResult validation{};
    PolicyValidationOptions options{};
    options.collect_all = false;
    options.include_warnings = true;

    // simulate exit code 3 path
    EXPECT_FALSE(validate_policy_file_semantic(result, &validation, options));
    EXPECT_FALSE(validation.ok);
}

} // namespace bytetaper::policy

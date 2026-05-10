// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/route_policy.h"
#include "policy/yaml_loader.h"

#include <fstream>
#include <gtest/gtest.h>
#include <iostream>

namespace bytetaper::policy {

TEST(YamlLoaderTest, ValidYamlSingleRoutePrefixDisabled) {
    const char* yaml = R"(
routes:
  - id: "test-id"
    match:
      kind: "prefix"
      prefix: "/test/"
    mutation: "disabled"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.count, 1u);
    EXPECT_STREQ(result.policies[0].route_id, "test-id");
    EXPECT_EQ(result.policies[0].match_kind, RouteMatchKind::Prefix);
    EXPECT_STREQ(result.policies[0].match_prefix, "/test/");
    EXPECT_EQ(result.policies[0].mutation, MutationMode::Disabled);
}

TEST(YamlLoaderTest, ValidYamlMutationHeadersOnly) {
    const char* yaml = R"(
routes:
  - id: "test-id"
    match:
      kind: "prefix"
      prefix: "/"
    mutation: "headers_only"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].mutation, MutationMode::HeadersOnly);
}

TEST(YamlLoaderTest, ValidYamlMutationFull) {
    const char* yaml = R"(
routes:
  - id: "test-id"
    match:
      kind: "prefix"
      prefix: "/"
    mutation: "full"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].mutation, MutationMode::Full);
}

TEST(YamlLoaderTest, ValidYamlExactMatchKind) {
    const char* yaml = R"(
routes:
  - id: "test-id"
    match:
      kind: "exact"
      prefix: "/exact"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].match_kind, RouteMatchKind::Exact);
}

TEST(YamlLoaderTest, ValidYamlMultipleRoutes) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/1" }
  - id: "r2"
    match: { kind: "prefix", prefix: "/2" }
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.count, 2u);
    EXPECT_STREQ(result.policies[1].route_id, "r2");
}

TEST(YamlLoaderTest, InvalidYamlMissingRoutes) {
    const char* yaml = "{}";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error, nullptr);
}

TEST(YamlLoaderTest, InvalidYamlMissingId) {
    const char* yaml = R"(
routes:
  - match: { kind: "prefix", prefix: "/1" }
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderTest, InvalidYamlMissingMatchPrefix) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix" }
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderTest, InvalidYamlBadMutationValue) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    mutation: "superfast"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderTest, InvalidYamlBadMatchKind) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "wildcard", prefix: "/" }
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderTest, NullContentReturnsError) {
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(nullptr, &result));
}

TEST(YamlLoaderTest, DefaultMutationIsDisabled) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].mutation, MutationMode::Disabled);
}

TEST(YamlLoaderTest, RoutePolicyPassesValidation) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/api" }
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(validate_route_policy(result.policies[0], nullptr));
}

TEST(YamlLoaderTest, DefaultFailureModeIsFailOpen) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].failure_mode, FailureMode::FailOpen);
}

TEST(YamlLoaderTest, ValidYamlFailureModeFailClosed) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    failure_mode: "fail_closed"
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_EQ(result.policies[0].failure_mode, FailureMode::FailClosed);
}

TEST(YamlLoaderTest, InvalidYamlBadFailureMode) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    failure_mode: "crash_now"
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
}

TEST(YamlLoaderTest, WaitWindowMsAloneBackwardCompatAndWarning) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    coalescing:
      enabled: true
      wait_window_ms: 100
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.policies[0].coalescing.backend_timeout_ms, 100u);
    EXPECT_EQ(result.policies[0].coalescing.handoff_buffer_ms, 50u);
    EXPECT_EQ(result.policies[0].coalescing.result_ready_retention_ms, 25u);
    EXPECT_NE(result.warning[0], '\0');
    EXPECT_TRUE(std::strstr(result.warning, "coalescing: 'wait_window_ms' is deprecated") !=
                nullptr);
}

TEST(YamlLoaderTest, WaitWindowMsWithBackendTimeoutMsReturnsError) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    coalescing:
      enabled: true
      wait_window_ms: 100
      backend_timeout_ms: 100
)";
    PolicyFileResult result{};
    EXPECT_FALSE(load_policy_from_string(yaml, &result));
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error, nullptr);
    EXPECT_TRUE(std::strstr(result.error, "both set") != nullptr);
}

TEST(YamlLoaderTest, NoWaitWindowMsNoWarning) {
    const char* yaml = R"(
routes:
  - id: "r1"
    match: { kind: "prefix", prefix: "/" }
    coalescing:
      enabled: true
      backend_timeout_ms: 100
      handoff_buffer_ms: 50
)";
    PolicyFileResult result{};
    EXPECT_TRUE(load_policy_from_string(yaml, &result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.warning[0], '\0');
}

TEST(YamlLoaderTest, ExamplePoliciesAreValidWithNoWarnings) {
    const char* files[] = { "examples/policy/coalescing-benchmark-policy.yaml",
                            "examples/policy/bytetaper-policy.yaml",
                            "examples/policy/observe-mode-policy.yaml" };

    for (const char* file : files) {
        PolicyFileResult result{};
        bool ok = load_policy_from_file(file, &result);
        if (!ok) {
            std::cout << "DEBUG: First try failed for " << file
                      << ", error: " << (result.error ? result.error : "null") << std::endl;
            // Try relative to build folder
            std::string alt_path = std::string("../") + file;
            PolicyFileResult result_alt1{};
            ok = load_policy_from_file(alt_path.c_str(), &result_alt1);
            if (ok) {
                EXPECT_TRUE(result_alt1.ok) << "Result ok is false for " << file;
                EXPECT_EQ(result_alt1.warning[0], '\0')
                    << "Warning found for " << file << ": " << result_alt1.warning;
            } else {
                std::cout << "DEBUG: Second try failed for " << alt_path
                          << ", error: " << (result_alt1.error ? result_alt1.error : "null")
                          << std::endl;
                // Try absolute path inside Docker workspace
                std::string abs_path = std::string("/workspace/") + file;
                PolicyFileResult result_alt2{};
                ok = load_policy_from_file(abs_path.c_str(), &result_alt2);
                if (ok) {
                    EXPECT_TRUE(result_alt2.ok) << "Result ok is false for " << file;
                    EXPECT_EQ(result_alt2.warning[0], '\0')
                        << "Warning found for " << file << ": " << result_alt2.warning;
                } else {
                    std::cout << "DEBUG: Third try failed for " << abs_path
                              << ", error: " << (result_alt2.error ? result_alt2.error : "null")
                              << std::endl;
                    FAIL() << "Failed to load " << file << " from any path";
                }
            }
        } else {
            EXPECT_TRUE(result.ok) << "Result ok is false for " << file;
            EXPECT_EQ(result.warning[0], '\0')
                << "Warning found for " << file << ": " << result.warning;
        }
    }
}

} // namespace bytetaper::policy

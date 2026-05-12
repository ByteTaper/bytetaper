// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_version.h"

#include <gtest/gtest.h>

using namespace bytetaper::taperquery;

TEST(TaperQueryPolicyIrVersionTest, IsSupportedPolicyIrVersion) {
    EXPECT_TRUE(is_supported_policy_ir_version("tq-ir/v1"));
    EXPECT_FALSE(is_supported_policy_ir_version("tq-ir/v2"));
    EXPECT_FALSE(is_supported_policy_ir_version(""));
}

TEST(TaperQueryPolicyIrVersionTest, IsSupportedIdentityVersion) {
    EXPECT_TRUE(is_supported_identity_version("policy-identity/v2"));
    EXPECT_FALSE(is_supported_identity_version("policy-identity/v1"));
    EXPECT_FALSE(is_supported_identity_version(""));
}

TEST(TaperQueryPolicyIrVersionTest, IsSupportedSourceSchemaVersion) {
    EXPECT_TRUE(is_supported_source_schema_version("tq/v1"));
    EXPECT_TRUE(is_supported_source_schema_version("yaml/v1"));
    EXPECT_FALSE(is_supported_source_schema_version("yaml/v2"));
    EXPECT_FALSE(is_supported_source_schema_version(""));
}

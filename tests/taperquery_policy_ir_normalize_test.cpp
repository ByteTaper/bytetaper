// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_normalize.h"

#include <gtest/gtest.h>

using namespace bytetaper::taperquery;

TEST(TaperQueryPolicyIrNormalizeTest, EmptyRoutePolicyNormalizes) {
    TqRoutePolicy policy;
    TqRoutePolicy normalized = normalize_route_policy_ir(policy);
    EXPECT_TRUE(normalized.route_id.empty());
    EXPECT_TRUE(normalized.cache.private_cache.auth_scope_header.empty());
    EXPECT_TRUE(normalized.cache.vary_headers.names.empty());
}

TEST(TaperQueryPolicyIrNormalizeTest, LowercaseAuthScopeHeader) {
    TqRoutePolicy policy;
    policy.cache.private_cache.auth_scope_header = "X-My-AUTH-Scope";
    TqRoutePolicy normalized = normalize_route_policy_ir(policy);
    EXPECT_EQ(normalized.cache.private_cache.auth_scope_header, "x-my-auth-scope");
}

TEST(TaperQueryPolicyIrNormalizeTest, LowercaseVaryHeaders) {
    TqRoutePolicy policy;
    policy.cache.vary_headers.names = { "Accept-Encoding", "X-CUSTOM-Header" };
    TqRoutePolicy normalized = normalize_route_policy_ir(policy);
    ASSERT_EQ(normalized.cache.vary_headers.names.size(), 2u);
    EXPECT_EQ(normalized.cache.vary_headers.names[0], "accept-encoding");
    EXPECT_EQ(normalized.cache.vary_headers.names[1], "x-custom-header");
}

TEST(TaperQueryPolicyIrNormalizeTest, StableVaryHeadersDedup) {
    TqRoutePolicy policy;
    policy.cache.vary_headers.names = { "Accept-Encoding", "accept-encoding", "X-Custom",
                                        "Accept-Encoding", "x-custom" };
    TqRoutePolicy normalized = normalize_route_policy_ir(policy);
    ASSERT_EQ(normalized.cache.vary_headers.names.size(), 2u);
    EXPECT_EQ(normalized.cache.vary_headers.names[0], "accept-encoding");
    EXPECT_EQ(normalized.cache.vary_headers.names[1], "x-custom");
}

TEST(TaperQueryPolicyIrNormalizeTest, PreserveFieldFiltersAndAlgorithms) {
    TqRoutePolicy policy;
    policy.field_filter.mode = TqFieldFilterMode::Allowlist;
    policy.field_filter.fields = { "FieldB", "FieldA", "FieldB" };
    policy.compression.preferred_algorithms = { TqCompressionAlgorithm::Zstd,
                                                TqCompressionAlgorithm::Brotli };

    TqRoutePolicy normalized = normalize_route_policy_ir(policy);
    ASSERT_EQ(normalized.field_filter.fields.size(), 3u);
    EXPECT_EQ(normalized.field_filter.fields[0], "FieldB");
    EXPECT_EQ(normalized.field_filter.fields[1], "FieldA");
    EXPECT_EQ(normalized.field_filter.fields[2], "FieldB");

    ASSERT_EQ(normalized.compression.preferred_algorithms.size(), 2u);
    EXPECT_EQ(normalized.compression.preferred_algorithms[0], TqCompressionAlgorithm::Zstd);
    EXPECT_EQ(normalized.compression.preferred_algorithms[1], TqCompressionAlgorithm::Brotli);
}

TEST(TaperQueryPolicyIrNormalizeTest, NormalizePolicyDocument) {
    TqPolicyDocument doc;
    doc.version.source_schema_version = "v1";
    doc.document_id = "doc123";
    doc.source_name = "git";

    TqRoutePolicy r1;
    r1.route_id = "r1";
    r1.cache.private_cache.auth_scope_header = "AUTHORIZATION";

    TqRoutePolicy r2;
    r2.route_id = "r2";
    r2.cache.vary_headers.names = { "X-Vary", "x-vary" };

    doc.routes = { r1, r2 };

    TqPolicyDocument normalized = normalize_policy_ir(doc);
    EXPECT_EQ(normalized.version.source_schema_version, "v1");
    EXPECT_EQ(normalized.version.policy_ir_version, "tq-ir/v1");
    EXPECT_EQ(normalized.version.identity_version, "policy-identity/v2");
    EXPECT_EQ(normalized.document_id, "doc123");
    EXPECT_EQ(normalized.source_name, "git");
    ASSERT_EQ(normalized.routes.size(), 2u);
    EXPECT_EQ(normalized.routes[0].route_id, "r1");
    EXPECT_EQ(normalized.routes[0].cache.private_cache.auth_scope_header, "authorization");
    EXPECT_EQ(normalized.routes[1].route_id, "r2");
    ASSERT_EQ(normalized.routes[1].cache.vary_headers.names.size(), 1u);
    EXPECT_EQ(normalized.routes[1].cache.vary_headers.names[0], "x-vary");
}

TEST(TaperQueryPolicyIrNormalizeTest, PreserveDeclarationOrderInDocument) {
    TqPolicyDocument doc;
    TqRoutePolicy r1;
    r1.route_id = "first";
    TqRoutePolicy r2;
    r2.route_id = "second";
    doc.routes = { r1, r2 };

    TqPolicyDocument normalized = normalize_policy_ir(doc);
    ASSERT_EQ(normalized.routes.size(), 2u);
    EXPECT_EQ(normalized.routes[0].route_id, "first");
    EXPECT_EQ(normalized.routes[1].route_id, "second");
}

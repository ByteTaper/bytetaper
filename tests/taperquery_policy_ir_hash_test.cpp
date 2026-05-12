// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_identity.h"

#include <gtest/gtest.h>

using namespace bytetaper::taperquery;

TEST(TaperQueryPolicyIrHashTest, Determinism) {
    TqRoutePolicy policy;
    policy.route_id = "test-route";
    policy.match_prefix = "/users";
    policy.cache.enabled = true;
    policy.cache.ttl_ms = 120000;

    std::string hash1 = compute_route_policy_identity(policy);
    std::string hash2 = compute_route_policy_identity(policy);

    EXPECT_EQ(hash1, hash2);
}

TEST(TaperQueryPolicyIrHashTest, FormatCorrectness) {
    TqRoutePolicy policy;
    std::string hash = compute_route_policy_identity(policy);
    EXPECT_EQ(hash.size(), 16u);
    for (char c : hash) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Non-hex char found: " << c;
    }
}

TEST(TaperQueryPolicyIrHashTest, RouteIdSensitivity) {
    TqRoutePolicy policy1;
    policy1.route_id = "routeA";
    TqRoutePolicy policy2;
    policy2.route_id = "routeB";

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, MatchPrefixSensitivity) {
    TqRoutePolicy policy1;
    policy1.match_prefix = "/items";
    TqRoutePolicy policy2;
    policy2.match_prefix = "/items/";

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, CacheSensitivity) {
    TqRoutePolicy policy1;
    policy1.cache.enabled = false;
    TqRoutePolicy policy2;
    policy2.cache.enabled = true;

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, CacheTtlSensitivity) {
    TqRoutePolicy policy1;
    policy1.cache.ttl_ms = 1000;
    TqRoutePolicy policy2;
    policy2.cache.ttl_ms = 2000;

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, PrivateCacheSensitivity) {
    TqRoutePolicy policy1;
    policy1.cache.private_cache.enabled = false;
    TqRoutePolicy policy2;
    policy2.cache.private_cache.enabled = true;

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, PrivateCacheAuthScopeSensitivity) {
    TqRoutePolicy policy1;
    policy1.cache.private_cache.auth_scope_header = "X-A";
    TqRoutePolicy policy2;
    policy2.cache.private_cache.auth_scope_header = "X-B";

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, FieldFilterSensitivity) {
    TqRoutePolicy policy1;
    policy1.field_filter.fields = { "a", "b" };
    TqRoutePolicy policy2;
    policy2.field_filter.fields = { "b", "a" }; // order change

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, CompressionSensitivity) {
    TqRoutePolicy policy1;
    policy1.compression.preferred_algorithms = { TqCompressionAlgorithm::Zstd };
    TqRoutePolicy policy2;
    policy2.compression.preferred_algorithms = { TqCompressionAlgorithm::Zstd,
                                                 TqCompressionAlgorithm::Brotli };

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, CoalescingSensitivity) {
    TqRoutePolicy policy1;
    policy1.coalescing.max_follower_wait_budget_ms = 100;
    TqRoutePolicy policy2;
    policy2.coalescing.max_follower_wait_budget_ms = 200;

    EXPECT_NE(compute_route_policy_identity(policy1), compute_route_policy_identity(policy2));
}

TEST(TaperQueryPolicyIrHashTest, DocumentIdentityComposition) {
    TqPolicyDocument doc1;
    doc1.version.source_schema_version = "v1";
    doc1.document_id = "docA";

    TqPolicyDocument doc2;
    doc2.version.source_schema_version = "v1";
    doc2.document_id = "docB";

    EXPECT_NE(compute_policy_document_identity(doc1), compute_policy_document_identity(doc2));
}

TEST(TaperQueryPolicyIrHashTest, DocumentBaseShaSensitivity) {
    TqPolicyDocument doc1;
    doc1.expected_base_sha = "sha1";
    TqPolicyDocument doc2;
    doc2.expected_base_sha = "sha2";

    EXPECT_EQ(compute_policy_document_identity(doc1), compute_policy_document_identity(doc2));
    EXPECT_NE(compute_policy_apply_request_identity(doc1),
              compute_policy_apply_request_identity(doc2));
}

TEST(TaperQueryPolicyIrHashTest, DocumentRouteOrderSensitivity) {
    TqRoutePolicy r1;
    r1.route_id = "r1";
    TqRoutePolicy r2;
    r2.route_id = "r2";

    TqPolicyDocument doc1;
    doc1.routes = { r1, r2 };

    TqPolicyDocument doc2;
    doc2.routes = { r2, r1 };

    EXPECT_NE(compute_policy_document_identity(doc1), compute_policy_document_identity(doc2));
}

TEST(TaperQueryPolicyIrHashTest, KnownEmptyHashCheck) {
    // Assert deterministic known hash on empty objects to guard against future accidental seed or
    // formatting regressions.
    TqRoutePolicy empty_route;
    std::string route_hash = compute_route_policy_identity(empty_route);
    // Placeholder to be updated on build-run
    EXPECT_EQ(route_hash, "f6f43c5661249b8c");

    TqPolicyDocument empty_doc;
    std::string doc_hash = compute_policy_document_identity(empty_doc);
    // Placeholder to be updated on build-run
    EXPECT_EQ(doc_hash, "d4e71d17c5cb4e10");
}

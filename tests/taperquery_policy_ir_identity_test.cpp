// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_identity.h"

#include <gtest/gtest.h>

using namespace bytetaper::taperquery;

TEST(TaperQueryPolicyIrIdentityTest, DeterminismAndFormat) {
    TqRoutePolicy route;
    route.route_id = "test-route";
    route.match_prefix = "/users";
    route.cache.enabled = true;
    route.cache.ttl_ms = 60000;

    std::string h1 = compute_route_policy_identity(route);
    std::string h2 = compute_route_policy_identity(route);
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.size(), 16u);

    for (char c : h1) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST(TaperQueryPolicyIrIdentityTest, VaryHeaderNormalization) {
    TqRoutePolicy r1;
    r1.route_id = "route-1";
    r1.cache.enabled = true;
    r1.cache.vary_headers.names = { "User-Agent", "Accept-Encoding" };

    TqRoutePolicy r2;
    r2.route_id = "route-1";
    r2.cache.enabled = true;
    r2.cache.vary_headers.names = { "user-agent", "accept-encoding" };

    EXPECT_EQ(compute_route_policy_identity(r1), compute_route_policy_identity(r2));
}

TEST(TaperQueryPolicyIrIdentityTest, ExcludedFieldsDoNotAlterContentIdentity) {
    TqPolicyDocument doc1;
    doc1.document_id = "policy-abc";
    doc1.expected_base_sha = "abc1234567890def";
    doc1.source_name = "foo.yaml";
    doc1.version.source_schema_version = "v1";

    TqPolicyDocument doc2;
    doc2.document_id = "policy-abc";
    doc2.expected_base_sha = "xyz9876543210zyx";
    doc2.source_name = "bar.yaml";
    doc2.version.source_schema_version = "v2";

    // Content identity must be IDENTICAL because only non-semantic/provenance fields differ.
    EXPECT_EQ(compute_policy_document_identity(doc1), compute_policy_document_identity(doc2));

    // Apply request identity must be DIFFERENT because expected_base_sha is semantic for the CAS
    // boundary.
    EXPECT_NE(compute_policy_apply_request_identity(doc1),
              compute_policy_apply_request_identity(doc2));
}

TEST(TaperQueryPolicyIrIdentityTest, RouteOrderSensitivity) {
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

TEST(TaperQueryPolicyIrIdentityTest, IdentitySensitivity) {
    TqRoutePolicy base;
    base.route_id = "r1";
    base.match_prefix = "/api";
    base.match_kind = TqRouteMatchKind::Prefix;
    base.allowed_method = TqHttpMethod::Get;
    base.mutation = TqMutationMode::Disabled;
    base.failure_mode = TqFailureMode::FailClosed;
    base.cache.enabled = true;
    base.cache.ttl_ms = 10000;
    base.cache.l1.enabled = true;
    base.cache.l1.capacity_entries = 1000;
    base.cache.l2.enabled = true;
    base.cache.l2.path = "/tmp/db";
    base.cache.private_cache.enabled = true;
    base.cache.private_cache.auth_scope_header = "Authorization";
    base.cache.field_variant.enabled = true;
    base.cache.field_variant.max_variants_per_route = 10;
    base.cache.vary_headers.names = { "Accept" };
    base.compression.enabled = true;
    base.compression.preferred_algorithms = { TqCompressionAlgorithm::Gzip,
                                              TqCompressionAlgorithm::Brotli };
    base.coalescing.enabled = true;
    base.coalescing.backend_timeout_ms = 5000;

    std::string base_hash = compute_route_policy_identity(base);

    // 1. Different route id changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.route_id = "r2";
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 2. Different match path changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.match_prefix = "/api/v2";
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 3. Different method changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.allowed_method = TqHttpMethod::Post;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 4. Different mutation changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.mutation = TqMutationMode::HeadersOnly;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 5. Different failure mode changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.failure_mode = TqFailureMode::FailOpen;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 6. Different cache TTL changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.ttl_ms = 20000;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 7. Different L1 capacity changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.l1.capacity_entries = 5000;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 8. Different L2 path changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.l2.path = "/tmp/db2";
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 9. Different private cache auth scope changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.private_cache.auth_scope_header = "X-User-Scope";
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 10. Different field variant settings change route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.field_variant.max_variants_per_route = 50;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 11. Different vary headers change route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.vary_headers.names = { "Accept", "X-Custom" };
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 12. Different compression algorithm order changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.compression.preferred_algorithms = { TqCompressionAlgorithm::Brotli,
                                                      TqCompressionAlgorithm::Gzip };
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 13. Different coalescing guardrail changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.coalescing.backend_timeout_ms = 15000;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 14. Cache invalidation enabled changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.invalidation.enabled = true;
        EXPECT_NE(compute_route_policy_identity(modified), base_hash);
    }

    // 15. Cache invalidation methods changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.invalidation.enabled = true;
        modified.cache.invalidation.on_methods = { "DELETE" };

        TqRoutePolicy modified2 = modified;
        modified2.cache.invalidation.on_methods = { "PATCH" };
        EXPECT_NE(compute_route_policy_identity(modified),
                  compute_route_policy_identity(modified2));
    }

    // 16. Cache invalidation timing changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.invalidation.enabled = true;
        modified.cache.invalidation.timing = "after_successful_upstream_response";

        TqRoutePolicy modified2 = modified;
        modified2.cache.invalidation.timing = "before_upstream_request";
        EXPECT_NE(compute_route_policy_identity(modified),
                  compute_route_policy_identity(modified2));
    }

    // 17. Cache invalidation success_status changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.invalidation.enabled = true;
        modified.cache.invalidation.success_status_min = 200;
        modified.cache.invalidation.success_status_max = 299;

        TqRoutePolicy modified2 = modified;
        modified2.cache.invalidation.success_status_min = 200;
        modified2.cache.invalidation.success_status_max = 399;
        EXPECT_NE(compute_route_policy_identity(modified),
                  compute_route_policy_identity(modified2));
    }

    // 18. Cache invalidation targets changes route identity.
    {
        TqRoutePolicy modified = base;
        modified.cache.invalidation.enabled = true;
        modified.cache.invalidation.targets.push_back(
            { "target1", TqCacheInvalidationStrategy::RouteEpoch });

        TqRoutePolicy modified2 = modified;
        modified2.cache.invalidation.targets[0].route_id = "target2";
        EXPECT_NE(compute_route_policy_identity(modified),
                  compute_route_policy_identity(modified2));

        TqRoutePolicy modified3 = modified;
        modified3.cache.invalidation.targets[0].strategy = TqCacheInvalidationStrategy::ExactKey;
        EXPECT_NE(compute_route_policy_identity(modified),
                  compute_route_policy_identity(modified3));
    }
}

TEST(TaperQueryPolicyIrIdentityTest, VersionFieldsChangeIdentity) {
    TqPolicyDocument doc1;
    doc1.document_id = "policy-abc";
    doc1.version.policy_ir_version = "v1";
    doc1.version.identity_version = "id-v1";

    TqPolicyDocument doc2 = doc1;
    doc2.version.identity_version = "id-v2";

    TqPolicyDocument doc3 = doc1;
    doc3.version.policy_ir_version = "v2";

    EXPECT_NE(compute_policy_document_identity(doc1), compute_policy_document_identity(doc2));
    EXPECT_NE(compute_policy_document_identity(doc1), compute_policy_document_identity(doc3));
}

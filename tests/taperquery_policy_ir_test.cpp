// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir.h"

#include <gtest/gtest.h>

using namespace bytetaper::taperquery;

TEST(TaperQueryPolicyIrTest, UnitHelpers) {
    EXPECT_EQ(duration_ms(250), 250u);
    EXPECT_EQ(duration_seconds(5), 5000u);
    EXPECT_EQ(size_bytes(1024), 1024u);
    EXPECT_EQ(size_kib(2), 2048u);
    EXPECT_EQ(size_mib(3), 3145728u);
}

TEST(TaperQueryPolicyIrTest, FieldFilterPolicyDefaults) {
    TqFieldFilterPolicy policy;
    EXPECT_EQ(policy.mode, TqFieldFilterMode::None);
    EXPECT_TRUE(policy.fields.empty());
}

TEST(TaperQueryPolicyIrTest, FieldVariantCachePolicyDefaults) {
    TqFieldVariantCachePolicy policy;
    EXPECT_FALSE(policy.enabled);
    EXPECT_EQ(policy.max_variants_per_route, 8u);
    EXPECT_EQ(policy.min_field_count, 1u);
    EXPECT_EQ(policy.max_field_count, 8u);
    EXPECT_EQ(policy.admission_threshold, 2u);
    EXPECT_EQ(policy.ttl_max_ms, 0u);
}

TEST(TaperQueryPolicyIrTest, CacheL1PolicyDefaults) {
    TqCacheL1Policy policy;
    EXPECT_FALSE(policy.enabled);
    EXPECT_EQ(policy.capacity_entries, 0u);
}

TEST(TaperQueryPolicyIrTest, CacheL2PolicyDefaults) {
    TqCacheL2Policy policy;
    EXPECT_FALSE(policy.enabled);
    EXPECT_TRUE(policy.path.empty());
}

TEST(TaperQueryPolicyIrTest, PrivateCachePolicyDefaults) {
    TqPrivateCachePolicy policy;
    EXPECT_FALSE(policy.enabled);
    EXPECT_TRUE(policy.auth_scope_header.empty());
}

TEST(TaperQueryPolicyIrTest, CachePolicyDefaults) {
    TqCachePolicy policy;
    EXPECT_EQ(policy.behavior, TqCacheBehavior::Default);
    EXPECT_EQ(policy.ttl_ms, 0u);
    EXPECT_FALSE(policy.enabled);
    EXPECT_FALSE(policy.private_cache.enabled);
    EXPECT_TRUE(policy.private_cache.auth_scope_header.empty());
}

TEST(TaperQueryPolicyIrTest, PaginationPolicyDefaults) {
    TqPaginationPolicy policy;
    EXPECT_FALSE(policy.enabled);
    EXPECT_EQ(policy.mode, TqPaginationMode::None);
    EXPECT_EQ(policy.limit_param, "limit");
    EXPECT_EQ(policy.offset_param, "offset");
    EXPECT_EQ(policy.default_limit, 0u);
    EXPECT_EQ(policy.max_limit, 0u);
    EXPECT_FALSE(policy.upstream_supports_pagination);
    EXPECT_EQ(policy.max_response_bytes_warning, 0u);
}

TEST(TaperQueryPolicyIrTest, CompressionPolicyDefaults) {
    TqCompressionPolicy policy;
    EXPECT_FALSE(policy.enabled);
    EXPECT_EQ(policy.min_size_bytes, 0u);
    EXPECT_TRUE(policy.eligible_content_types.empty());
    EXPECT_TRUE(policy.preferred_algorithms.empty());
    EXPECT_EQ(policy.already_encoded_behavior, TqAlreadyEncodedBehavior::Skip);
}

TEST(TaperQueryPolicyIrTest, CoalescingPolicyDefaults) {
    TqCoalescingPolicy policy;
    EXPECT_FALSE(policy.enabled);
    EXPECT_EQ(policy.mode, TqCoalescingMode::CacheAssisted);
    EXPECT_EQ(policy.backend_timeout_ms, 500u);
    EXPECT_EQ(policy.handoff_buffer_ms, 250u);
    EXPECT_EQ(policy.result_ready_retention_ms, 50u);
    EXPECT_EQ(policy.max_waiters_per_key, 64u);
    EXPECT_TRUE(policy.require_cache_enabled);
    EXPECT_FALSE(policy.allow_authenticated);
    EXPECT_EQ(policy.max_follower_wait_budget_ms, 0u);
    EXPECT_EQ(policy.max_active_follower_waiters, 0u);
    EXPECT_EQ(policy.max_active_follower_waiters_per_shard, 0u);
}

TEST(TaperQueryPolicyIrTest, RoutePolicyDefaultsAndAssignment) {
    TqRoutePolicy policy;
    EXPECT_TRUE(policy.route_id.empty());
    EXPECT_TRUE(policy.match_prefix.empty());
    EXPECT_EQ(policy.match_kind, TqRouteMatchKind::Prefix);
    EXPECT_EQ(policy.mutation, TqMutationMode::Disabled);
    EXPECT_EQ(policy.allowed_method, TqHttpMethod::Any);
    EXPECT_EQ(policy.max_response_bytes, 0u);
    EXPECT_EQ(policy.failure_mode, TqFailureMode::FailOpen);

    policy.route_id = "test-route";
    policy.match_prefix = "/items";
    policy.match_kind = TqRouteMatchKind::Exact;
    policy.mutation = TqMutationMode::Full;
    policy.allowed_method = TqHttpMethod::Get;
    policy.max_response_bytes = size_mib(10);

    EXPECT_EQ(policy.route_id, "test-route");
    EXPECT_EQ(policy.match_prefix, "/items");
    EXPECT_EQ(policy.match_kind, TqRouteMatchKind::Exact);
    EXPECT_EQ(policy.mutation, TqMutationMode::Full);
    EXPECT_EQ(policy.allowed_method, TqHttpMethod::Get);
    EXPECT_EQ(policy.max_response_bytes, 10485760u);
}

TEST(TaperQueryPolicyIrTest, PolicyDocumentConstruction) {
    TqPolicyDocument doc;
    EXPECT_TRUE(doc.schema_version.empty());
    EXPECT_TRUE(doc.document_id.empty());
    EXPECT_TRUE(doc.source_name.empty());
    EXPECT_TRUE(doc.expected_base_sha.empty());
    EXPECT_TRUE(doc.routes.empty());

    doc.schema_version = "v1";
    doc.document_id = "doc-1";
    doc.source_name = "git";
    doc.expected_base_sha = "abcdef0123456789";

    EXPECT_EQ(doc.schema_version, "v1");
    EXPECT_EQ(doc.document_id, "doc-1");
    EXPECT_EQ(doc.source_name, "git");
    EXPECT_EQ(doc.expected_base_sha, "abcdef0123456789");
}

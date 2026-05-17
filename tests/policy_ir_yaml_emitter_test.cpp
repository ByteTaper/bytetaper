// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_yaml_emitter.h"

#include <gtest/gtest.h>

using namespace bytetaper::taperquery;

TEST(PolicyIrYamlEmitterTest, EmitEmptyDocument) {
    TqPolicyDocument doc;
    doc.document_id = "empty-doc";
    doc.source_name = "test-emitter";
    doc.expected_base_sha = "0000000000000000000000000000000000000000";
    doc.version.source_schema_version = "v1";
    doc.version.policy_ir_version = "tq-ir/v1";
    doc.version.identity_version = "policy-identity/v2";

    PolicyIrYamlEmitResult res = emit_policy_ir_canonical_yaml(doc);
    EXPECT_TRUE(res.ok);
    std::string yaml = res.yaml;
    EXPECT_FALSE(yaml.empty());
    EXPECT_NE(yaml.find("empty-doc"), std::string::npos);
    EXPECT_NE(yaml.find("test-emitter"), std::string::npos);
    EXPECT_NE(yaml.find("0000000000000000000000000000000000000000"), std::string::npos);
    EXPECT_NE(yaml.find("tq-ir/v1"), std::string::npos);
}

TEST(PolicyIrYamlEmitterTest, EmitDocumentWithRoutes) {
    TqPolicyDocument doc;
    doc.document_id = "complex-doc";
    doc.source_name = "test-emitter";
    doc.expected_base_sha = "1234567890abcdef1234567890abcdef12345678";
    doc.version.source_schema_version = "v1";
    doc.version.policy_ir_version = "tq-ir/v1";
    doc.version.identity_version = "policy-identity/v2";

    TqRoutePolicy r1;
    r1.route_id = "route-get-items";
    r1.match_prefix = "/items";
    r1.match_kind = TqRouteMatchKind::Prefix;
    r1.mutation = TqMutationMode::Disabled;
    r1.allowed_method = TqHttpMethod::Get;
    r1.max_response_bytes = 1024 * 1024;
    r1.failure_mode = TqFailureMode::FailOpen;

    // Field filter
    r1.field_filter.mode = TqFieldFilterMode::Denylist;
    r1.field_filter.fields = { "password", "ssn" };

    // Cache
    r1.cache.enabled = true;
    r1.cache.behavior = TqCacheBehavior::Store;
    r1.cache.ttl_ms = 30000;
    r1.cache.l1.enabled = true;
    r1.cache.l1.capacity_entries = 1000;
    r1.cache.l2.enabled = true;
    r1.cache.l2.path = "/tmp/l2-cache";
    r1.cache.private_cache.enabled = true;
    r1.cache.private_cache.auth_scope_header = "Authorization";

    // Field variant cache
    r1.cache.field_variant.enabled = true;
    r1.cache.field_variant.max_variants_per_route = 16;
    r1.cache.field_variant.min_field_count = 2;
    r1.cache.field_variant.max_field_count = 10;
    r1.cache.field_variant.admission_threshold = 3;
    r1.cache.field_variant.ttl_max_ms = 60000;

    // Vary headers
    r1.cache.vary_headers.names = { "Accept-Encoding", "X-Custom" };

    // Invalidation
    r1.cache.invalidation.enabled = true;
    r1.cache.invalidation.on_methods = { "PUT", "DELETE" };
    r1.cache.invalidation.timing = "after_successful_upstream_response";
    r1.cache.invalidation.success_status_min = 200;
    r1.cache.invalidation.success_status_max = 299;
    TqCacheInvalidationTarget target;
    target.route_id = "route-get-items";
    target.strategy = TqCacheInvalidationStrategy::RouteEpoch;
    r1.cache.invalidation.targets.push_back(target);

    // Pagination
    r1.pagination.enabled = true;
    r1.pagination.mode = TqPaginationMode::LimitOffset;
    r1.pagination.limit_param = "page_size";
    r1.pagination.offset_param = "page_offset";
    r1.pagination.default_limit = 20;
    r1.pagination.max_limit = 100;
    r1.pagination.upstream_supports_pagination = true;
    r1.pagination.max_response_bytes_warning = 50000;

    // Compression
    r1.compression.enabled = true;
    r1.compression.min_size_bytes = 2048;
    r1.compression.eligible_content_types = { "application/json", "text/plain" };
    r1.compression.preferred_algorithms = { TqCompressionAlgorithm::Gzip,
                                            TqCompressionAlgorithm::Brotli };
    r1.compression.already_encoded_behavior = TqAlreadyEncodedBehavior::Passthrough;

    // Coalescing
    r1.coalescing.enabled = true;
    r1.coalescing.mode = TqCoalescingMode::CacheAssisted;
    r1.coalescing.backend_timeout_ms = 1000;
    r1.coalescing.handoff_buffer_ms = 500;
    r1.coalescing.result_ready_retention_ms = 100;
    r1.coalescing.max_waiters_per_key = 128;
    r1.coalescing.require_cache_enabled = false;
    r1.coalescing.allow_authenticated = true;
    r1.coalescing.max_follower_wait_budget_ms = 2000;
    r1.coalescing.max_active_follower_waiters = 50;
    r1.coalescing.max_active_follower_waiters_per_shard = 10;

    doc.routes.push_back(r1);

    PolicyIrYamlEmitResult res = emit_policy_ir_canonical_yaml(doc);
    EXPECT_TRUE(res.ok);
    std::string yaml = res.yaml;
    EXPECT_FALSE(yaml.empty());

    // Check key fields presence
    EXPECT_NE(yaml.find("- id: route-get-items"), std::string::npos);
    EXPECT_NE(yaml.find("prefix: /items"), std::string::npos);
    EXPECT_NE(yaml.find("method: get"), std::string::npos);
    EXPECT_NE(yaml.find("max_response_bytes: 1048576"), std::string::npos);

    // Field filter
    EXPECT_NE(yaml.find("mode: denylist"), std::string::npos);
    EXPECT_NE(yaml.find("- password"), std::string::npos);
    EXPECT_NE(yaml.find("- ssn"), std::string::npos);

    // Cache
    EXPECT_NE(yaml.find("ttl_seconds: 30"), std::string::npos);
    EXPECT_NE(yaml.find("capacity_entries: 1000"), std::string::npos);
    EXPECT_NE(yaml.find("path: /tmp/l2-cache"), std::string::npos);
    EXPECT_NE(yaml.find("auth_scope_header: authorization"), std::string::npos);

    // Field variant
    EXPECT_NE(yaml.find("max_variants_per_route: 16"), std::string::npos);
    EXPECT_NE(yaml.find("admission_threshold: 3"), std::string::npos);

    // Vary headers
    EXPECT_NE(yaml.find("- accept-encoding"), std::string::npos);

    // Invalidation
    EXPECT_NE(yaml.find("- PUT"), std::string::npos);
    EXPECT_NE(yaml.find("timing: after_successful_upstream_response"), std::string::npos);
    EXPECT_NE(yaml.find("strategy: route_epoch"), std::string::npos);

    // Pagination
    EXPECT_NE(yaml.find("limit_param: page_size"), std::string::npos);

    // Compression
    EXPECT_NE(yaml.find("min_size_bytes: 2048"), std::string::npos);
    EXPECT_NE(yaml.find("- gzip"), std::string::npos);
    EXPECT_NE(yaml.find("already_encoded_behavior: passthrough"), std::string::npos);

    // Coalescing
    EXPECT_NE(yaml.find("backend_timeout_ms: 1000"), std::string::npos);
    EXPECT_NE(yaml.find("max_follower_wait_budget_ms: 2000"), std::string::npos);
}

#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

TEST(PolicyIrYamlEmitterTest, EmitLoadParityAndDeterminism) {
    TqRoutePolicy r1;
    r1.route_id = "route-1";
    r1.match_prefix = "/api";
    r1.match_kind = TqRouteMatchKind::Prefix;
    r1.allowed_method = TqHttpMethod::Get;
    r1.cache.enabled = true;
    r1.cache.ttl_ms = 30000;
    r1.cache.l1.enabled = true;
    r1.cache.l1.capacity_entries = 1000;
    r1.compression.enabled = true;
    r1.compression.min_size_bytes = 1024;

    // Create temporary file first to get the dynamic tmp_path
    char tmp_path[] = "/tmp/bytetaper_emitter_parity_XXXXXX";
    int fd = mkstemp(tmp_path);
    ASSERT_NE(fd, -1);

    TqPolicyDocument doc;
    doc.document_id = "test-doc-123";
    doc.expected_base_sha = "abc123expected";
    doc.version.source_schema_version = "yaml/v1";
    doc.version.policy_ir_version = "tq-ir/v1";
    doc.version.identity_version = "policy-identity/v2";
    doc.source_name = tmp_path;
    doc.routes.push_back(r1);

    // 1. Emit to YAML
    PolicyIrYamlEmitResult emit_res1 = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit_res1.ok);
    std::string yaml1 = emit_res1.yaml;

    ssize_t written = write(fd, yaml1.data(), yaml1.size());
    close(fd);
    ASSERT_EQ(written, static_cast<ssize_t>(yaml1.size()));

    // 2. Load the YAML back
    auto load_res = load_policy_ir_from_yaml_file(tmp_path);
    std::remove(tmp_path);
    ASSERT_TRUE(load_res.ok) << load_res.error;
    TqPolicyDocument parsed_doc = load_res.policy;

    // 3. Verify Identity Parity
    std::string original_identity = compute_policy_document_identity(doc);
    std::string parsed_identity = compute_policy_document_identity(parsed_doc);
    EXPECT_EQ(original_identity, parsed_identity);

    // 4. Emit parsed document and verify byte-for-byte determinism
    PolicyIrYamlEmitResult emit_res2 = emit_policy_ir_canonical_yaml(parsed_doc);
    ASSERT_TRUE(emit_res2.ok);
    std::string yaml2 = emit_res2.yaml;
    EXPECT_EQ(yaml1, yaml2);
}

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_validator.h"
#include "taperquery/policy_ir_version.h"
#include "taperquery/route_analysis.h"
#include "taperquery/tq_plan.h"

#include <gtest/gtest.h>

namespace bytetaper::taperquery {

TEST(TaperQueryPolicyIrValidatorTest, VersionValidationDiagnostics) {
    // Valid version info
    TqPolicyVersionInfo valid_version;
    valid_version.source_schema_version = "tq/v1";
    valid_version.policy_ir_version = std::string(kCurrentPolicyIrVersion);
    valid_version.identity_version = std::string(kCurrentPolicyIdentityVersion);

    auto res_valid = validate_policy_version_info(valid_version);
    EXPECT_TRUE(res_valid.ok);
    EXPECT_TRUE(res_valid.issues.empty());

    // Invalid version info
    TqPolicyVersionInfo invalid_version;
    invalid_version.source_schema_version = "invalid_src";
    invalid_version.policy_ir_version = "invalid_ir";
    invalid_version.identity_version = "invalid_id";

    auto res_invalid = validate_policy_version_info(invalid_version);
    EXPECT_FALSE(res_invalid.ok);
    EXPECT_GE(res_invalid.issues.size(), 2u);

    bool found_source = false;
    bool found_ir = false;
    for (const auto& issue : res_invalid.issues) {
        if (issue.field == "version.source_schema_version") {
            found_source = true;
        }
        if (issue.field == "version.policy_ir_version") {
            found_ir = true;
        }
    }
    EXPECT_TRUE(found_source);
    EXPECT_TRUE(found_ir);
}

TEST(TaperQueryPolicyIrValidatorTest, RouteSemanticChecks) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    // 1. Missing path/prefix
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = ""; // invalid
        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        ASSERT_GE(result.issues.size(), 1u);
        EXPECT_EQ(result.issues[0].field_path, "match_prefix");
    }

    // 2. Cache validations (enabled without ttl, non-GET cache store)
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.allowed_method =
            TqHttpMethod::Post; // POST method with cache enabled with store behavior
        route.cache.enabled = true;
        route.cache.behavior = TqCacheBehavior::Store;
        route.cache.ttl_ms = 0; // ttl_ms = 0
        route.cache.l1.enabled = true;
        route.cache.l1.capacity_entries = 1000;

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);

        bool found_ttl = false;
        bool found_method = false;
        for (const auto& issue : result.issues) {
            if (issue.field_path == "cache.ttl_ms")
                found_ttl = true;
            if (issue.field_path == "cache" &&
                issue.reason.find("only supported") != std::string::npos)
                found_method = true;
        }
        EXPECT_TRUE(found_ttl);
        EXPECT_TRUE(found_method);
    }

    // 3. Field filter: Allowlist but empty fields list, and too many fields
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.field_filter.mode = TqFieldFilterMode::Allowlist;
        route.field_filter.fields.clear(); // empty allowlist

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        ASSERT_GE(result.issues.size(), 1u);
        EXPECT_EQ(result.issues[0].field_path, "field_filter.fields");
    }

    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.field_filter.mode = TqFieldFilterMode::Denylist;
        for (int i = 0; i < 20; ++i) {
            route.field_filter.fields.push_back("field_" + std::to_string(i));
        }

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        ASSERT_GE(result.issues.size(), 1u);
        EXPECT_EQ(result.issues[0].field_path, "field_filter.fields");
        EXPECT_NE(result.issues[0].reason.find("too many"), std::string::npos);
    }

    // 4. Coalescing: coalesce timeout/budget sanity checks
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.allowed_method = TqHttpMethod::Get;
        route.cache.enabled = true;
        route.cache.ttl_ms = 5000;
        route.cache.l1.enabled = true;
        route.cache.l1.capacity_entries = 1000;
        route.coalescing.enabled = true;
        route.coalescing.mode = TqCoalescingMode::CacheAssisted;
        route.coalescing.backend_timeout_ms = 28000;
        route.coalescing.handoff_buffer_ms = 4000; // total 32000 > 30000

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found_budget = false;
        for (const auto& issue : result.issues) {
            if (issue.reason.find("exceeds follower maximum") != std::string::npos)
                found_budget = true;
        }
        EXPECT_TRUE(found_budget);
    }

    // 5. Coalescing require_cache_enabled check
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.allowed_method = TqHttpMethod::Get;
        route.coalescing.enabled = true;
        route.coalescing.mode = TqCoalescingMode::CacheAssisted;
        route.coalescing.require_cache_enabled = true;
        route.cache.enabled = false; // invalid

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found_cache_req = false;
        for (const auto& issue : result.issues) {
            if (issue.reason.find("requires cache to be enabled") != std::string::npos)
                found_cache_req = true;
        }
        EXPECT_TRUE(found_cache_req);
    }

    // 6. Pagination check: default limit > max limit
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.allowed_method = TqHttpMethod::Get;
        route.pagination.enabled = true;
        route.pagination.mode = TqPaginationMode::LimitOffset;
        route.pagination.default_limit = 100;
        route.pagination.max_limit = 50; // invalid: max_limit < default_limit
        route.pagination.limit_param = "limit";
        route.pagination.offset_param = "offset";

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found_pag = false;
        for (const auto& issue : result.issues) {
            if (issue.field_path == "pagination.max_limit")
                found_pag = true;
        }
        EXPECT_TRUE(found_pag);
    }

    // 7. Compression: Preferred algorithms contains 'none'
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.allowed_method = TqHttpMethod::Get;
        route.compression.enabled = true;
        route.compression.min_size_bytes = 1000;
        route.compression.eligible_content_types = { "application/json" };
        route.compression.preferred_algorithms = { TqCompressionAlgorithm::Gzip,
                                                   TqCompressionAlgorithm::None };

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found_algo = false;
        for (const auto& issue : result.issues) {
            if (issue.field_path == "compression.preferred_algorithms")
                found_algo = true;
        }
        EXPECT_TRUE(found_algo);
    }
}

TEST(TaperQueryPolicyIrValidatorTest, RouteAnalysisPrecedenceAndShadowing) {
    TqPolicyDocument doc;
    doc.version.source_schema_version = "tq/v1";
    doc.version.policy_ir_version = std::string(kCurrentPolicyIrVersion);
    doc.version.identity_version = std::string(kCurrentPolicyIdentityVersion);

    // 1. Prefix shadowing: Route 1 is a broad prefix route declared first,
    // Route 2 is a more specific route. It gets shadowed!
    {
        TqRoutePolicy r1;
        r1.route_id = "broad_route";
        r1.match_prefix = "/api";
        r1.match_kind = TqRouteMatchKind::Prefix;
        r1.allowed_method = TqHttpMethod::Any;

        TqRoutePolicy r2;
        r2.route_id = "narrow_route";
        r2.match_prefix = "/api/orders";
        r2.match_kind = TqRouteMatchKind::Exact;
        r2.allowed_method = TqHttpMethod::Get;

        doc.routes = { r1, r2 };

        auto report = analyze_taperquery_route_precedence(doc);
        bool found_shadow = false;
        for (const auto& finding : report.findings) {
            if (finding.kind == TqRouteAnalysisKind::PrefixShadowsExact &&
                finding.route_id == "narrow_route") {
                found_shadow = true;
            }
        }
        EXPECT_TRUE(found_shadow);
    }

    // 2. Exact Duplicates Conflict
    {
        TqRoutePolicy r1;
        r1.route_id = "route_1";
        r1.match_prefix = "/api/v1";
        r1.match_kind = TqRouteMatchKind::Exact;
        r1.allowed_method = TqHttpMethod::Get;

        TqRoutePolicy r2;
        r2.route_id = "route_2";
        r2.match_prefix = "/api/v1";
        r2.match_kind = TqRouteMatchKind::Exact;
        r2.allowed_method = TqHttpMethod::Get;

        doc.routes = { r1, r2 };

        auto report = analyze_taperquery_route_precedence(doc);
        bool found_dup = false;
        for (const auto& finding : report.findings) {
            if (finding.kind == TqRouteAnalysisKind::DuplicateExactMatch) {
                found_dup = true;
            }
        }
        EXPECT_TRUE(found_dup);
    }

    // 3. Shared L2 Path Warning
    {
        TqRoutePolicy r1;
        r1.route_id = "route_1";
        r1.match_prefix = "/api/v1";
        r1.match_kind = TqRouteMatchKind::Exact;
        r1.allowed_method = TqHttpMethod::Get;
        r1.cache.enabled = true;
        r1.cache.ttl_ms = 5000;
        r1.cache.l2.enabled = true;
        r1.cache.l2.path = "/var/lib/cache_shared";

        TqRoutePolicy r2;
        r2.route_id = "route_2";
        r2.match_prefix = "/api/v2";
        r2.match_kind = TqRouteMatchKind::Exact;
        r2.allowed_method = TqHttpMethod::Get;
        r2.cache.enabled = true;
        r2.cache.ttl_ms = 5000;
        r2.cache.l2.enabled = true;
        r2.cache.l2.path = "/var/lib/cache_shared"; // shared path!

        doc.routes = { r1, r2 };

        auto report = analyze_taperquery_route_precedence(doc);
        bool found_shared = false;
        for (const auto& finding : report.findings) {
            if (finding.kind == TqRouteAnalysisKind::SharedL2CachePath) {
                found_shared = true;
            }
        }
        EXPECT_TRUE(found_shared);
    }
}

TEST(TaperQueryPolicyIrValidatorTest, DuplicateVaryHeadersNormalization) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    TqRoutePolicy route;
    route.route_id = "test_route";
    route.match_prefix = "/api";
    route.cache.enabled = true;
    route.cache.ttl_ms = 5000;
    route.cache.l1.enabled = true;
    route.cache.l1.capacity_entries = 1000;
    route.cache.vary_headers.names = { "User-Agent",
                                       "user-agent" }; // duplicate after normalization!

    auto result = validate_taperquery_route_policy_ir(route, options);
    EXPECT_FALSE(result.ok); // must fail with Error under updated GA contracts!
    bool found_error = false;
    for (const auto& issue : result.issues) {
        if (issue.severity == TqPolicyValidationSeverity::Error &&
            issue.field_path == "cache.vary_headers.names" &&
            issue.code == "VARY_HEADER_DUPLICATE") {
            found_error = true;
        }
    }
    EXPECT_TRUE(found_error);
}

TEST(TaperQueryPolicyIrValidatorTest, NewPolicyIrValidatorAdditionalGaps) {
    TqPolicyValidationOptions options;
    options.collect_all = true;

    // 1. None mode with fields
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.field_filter.mode = TqFieldFilterMode::None;
        route.field_filter.fields = { "id", "name" };

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "NONE_MODE_WITH_FIELDS") {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    // 2. FVC with cache disabled
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.cache.enabled = false;
        route.cache.field_variant.enabled = true;

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "FVC_WITHOUT_CACHE") {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    // 3. Field filter name length > 64
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.field_filter.mode = TqFieldFilterMode::Allowlist;
        // 65 chars field name
        route.field_filter.fields = { std::string(65, 'a') };

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "FIELD_FILTER_NAME_TOO_LONG") {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    // 4. Compression content type > 8
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.compression.enabled = true;
        route.compression.min_size_bytes = 100;
        route.compression.preferred_algorithms = { TqCompressionAlgorithm::Gzip };
        route.compression.eligible_content_types = { "a", "b", "c", "d", "e", "f", "g", "h", "i" };

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "COMPRESSION_CONTENT_TYPES_LIMIT") {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    // 5. Compression content type length > 64
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        route.compression.enabled = true;
        route.compression.min_size_bytes = 100;
        route.compression.preferred_algorithms = { TqCompressionAlgorithm::Gzip };
        // 65 chars content type
        route.compression.eligible_content_types = { std::string(65, 'a') };

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "COMPRESSION_CONTENT_TYPE_TOO_LONG") {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    // 6. Invalid enums range validation
    {
        TqRoutePolicy route;
        route.route_id = "test_route";
        route.match_prefix = "/api";
        // Invalid enums via static_cast
        route.match_kind = static_cast<TqRouteMatchKind>(99);
        route.mutation = static_cast<TqMutationMode>(99);
        route.allowed_method = static_cast<TqHttpMethod>(99);
        route.failure_mode = static_cast<TqFailureMode>(99);

        auto result = validate_taperquery_route_policy_ir(route, options);
        EXPECT_FALSE(result.ok);
        bool found_kind = false;
        bool found_mutation = false;
        bool found_method = false;
        bool found_failure = false;
        for (const auto& issue : result.issues) {
            if (issue.code == "INVALID_ROUTE_MATCH_KIND")
                found_kind = true;
            if (issue.code == "INVALID_MUTATION_MODE")
                found_mutation = true;
            if (issue.code == "INVALID_HTTP_METHOD")
                found_method = true;
            if (issue.code == "INVALID_FAILURE_MODE")
                found_failure = true;
        }
        EXPECT_TRUE(found_kind);
        EXPECT_TRUE(found_mutation);
        EXPECT_TRUE(found_method);
        EXPECT_TRUE(found_failure);
    }
}

} // namespace bytetaper::taperquery

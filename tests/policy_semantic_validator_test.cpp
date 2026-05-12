// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/policy_semantic_validator.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::policy {

namespace {

static RoutePolicy make_minimal_valid_route() {
    RoutePolicy r{};
    r.route_id = "r1";
    r.match_prefix = "/api/v1";
    r.match_kind = RouteMatchKind::Prefix;
    r.mutation = MutationMode::Full;
    r.allowed_method = HttpMethod::Get;
    return r;
}

static CompressionPolicy make_valid_compression() {
    CompressionPolicy p{};
    p.enabled = true;
    p.min_size_bytes = 1024;
    std::strcpy(p.eligible_content_types[0], "application/json");
    p.eligible_content_type_count = 1;
    p.preferred_algorithms[0] = CompressionAlgorithm::Gzip;
    p.preferred_algorithm_count = 1;
    p.already_encoded_behavior = AlreadyEncodedBehavior::Skip;
    return p;
}

static CachePolicy make_valid_cache() {
    CachePolicy p{};
    p.enabled = true;
    p.ttl_seconds = 60;
    p.behavior = CacheBehavior::Store;
    p.l1.enabled = true;
    return p;
}

static PaginationPolicy make_valid_pagination() {
    PaginationPolicy p{};
    p.enabled = true;
    p.mode = PaginationMode::LimitOffset;
    p.default_limit = 10;
    p.max_limit = 100;
    p.upstream_supports_pagination = true;
    std::strcpy(p.limit_param, "limit");
    std::strcpy(p.offset_param, "offset");
    return p;
}

static CoalescingPolicy make_valid_coalescing() {
    CoalescingPolicy p{};
    p.enabled = true;
    p.backend_timeout_ms = 5000;
    p.handoff_buffer_ms = 100;
    p.result_ready_retention_ms = 50;
    p.max_waiters_per_key = 10;
    p.mode = CoalescingMode::CacheAssisted;
    p.require_cache_enabled = false;
    return p;
}

} // namespace

// ==========================================
// Route-level Tests
// ==========================================

TEST(PolicySemanticValidatorTest, AcceptsMinimalValidDisabledRoute) {
    auto route = make_minimal_valid_route();
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_TRUE(validate_route_policy_semantic(route, &result, options));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.issue_count, 0);
}

TEST(PolicySemanticValidatorTest, RejectsMissingRouteId) {
    auto route = make_minimal_valid_route();
    route.route_id = nullptr;
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason, "route_id is required");
}

TEST(PolicySemanticValidatorTest, RejectsInvalidPrefix) {
    auto route = make_minimal_valid_route();
    route.match_prefix = "api/v1"; // missing '/'
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason, "match_prefix must start with '/'");
}

TEST(PolicySemanticValidatorTest, RejectsCacheEnabledForPostRoute) {
    auto route = make_minimal_valid_route();
    route.allowed_method = HttpMethod::Post;
    route.cache = make_valid_cache();
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Cache);
    EXPECT_STREQ(result.issues[0].reason, "cache not supported for non-GET routes");
}

TEST(PolicySemanticValidatorTest, RejectsPrivateCacheWithoutAuthScope) {
    auto route = make_minimal_valid_route();
    route.cache = make_valid_cache();
    route.cache.private_cache = true;
    route.cache.auth_scope_header[0] = '\0'; // empty auth scope
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Cache);
    EXPECT_STREQ(result.issues[0].reason,
                 "cache.private_cache requires auth_scope_header to be set");
}

TEST(PolicySemanticValidatorTest, RejectsCompressionEnabledWithoutContentTypes) {
    auto route = make_minimal_valid_route();
    route.compression = make_valid_compression();
    route.compression.eligible_content_type_count = 0;
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Compression);
    EXPECT_STREQ(result.issues[0].reason, "compression enabled with empty eligible_content_types");
}

TEST(PolicySemanticValidatorTest, RejectsCompressionEnabledWithoutPreferredAlgorithms) {
    auto route = make_minimal_valid_route();
    route.compression = make_valid_compression();
    route.compression.preferred_algorithm_count = 0;
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Compression);
    EXPECT_STREQ(result.issues[0].reason, "compression enabled with no preferred_algorithms");
}

TEST(PolicySemanticValidatorTest, RejectsPaginationWithoutMaxLimit) {
    auto route = make_minimal_valid_route();
    route.pagination = make_valid_pagination();
    route.pagination.max_limit = 0; // standard validation safe requirement
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Pagination);
    EXPECT_STREQ(result.issues[0].reason,
                 "pagination.max_limit required when pagination is enabled");
}

TEST(PolicySemanticValidatorTest, RejectsCursorPaginationAsNotImplemented) {
    auto route = make_minimal_valid_route();
    route.pagination = make_valid_pagination();
    route.pagination.mode = PaginationMode::Cursor;
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Pagination);
    EXPECT_STREQ(result.issues[0].reason, "cursor_mode_not_implemented");
}

TEST(PolicySemanticValidatorTest, RejectsCoalescingWithoutCacheWhenRequired) {
    auto route = make_minimal_valid_route();
    route.coalescing = make_valid_coalescing();
    route.coalescing.require_cache_enabled = true;
    route.cache.enabled = false;
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Coalescing);
    EXPECT_STREQ(result.issues[0].reason, "coalescing requires cache to be enabled");
}

TEST(PolicySemanticValidatorTest, RejectsCoalescingForPostRoute) {
    auto route = make_minimal_valid_route();
    route.allowed_method = HttpMethod::Post;
    route.coalescing = make_valid_coalescing();
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Coalescing);
    EXPECT_STREQ(result.issues[0].reason, "coalescing only supported for GET or ANY routes");
}

TEST(PolicySemanticValidatorTest, RejectsCoalescingHandoffBufferGreaterThanBackendTimeout) {
    auto route = make_minimal_valid_route();
    route.coalescing = make_valid_coalescing();
    route.coalescing.handoff_buffer_ms = 4000;
    route.coalescing.backend_timeout_ms = 3000;
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Coalescing);
    EXPECT_STREQ(result.issues[0].reason,
                 "coalescing handoff_buffer_ms exceeds backend_timeout_ms");
}

TEST(PolicySemanticValidatorTest, RejectsHeadersOnlyWithFieldFilter) {
    auto route = make_minimal_valid_route();
    route.mutation = MutationMode::HeadersOnly;
    route.field_filter.mode = FieldFilterMode::Denylist; // enabled
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason, "field_filter requires mutation=full");
}

TEST(PolicySemanticValidatorTest, RejectsHeadersOnlyWithCompression) {
    auto route = make_minimal_valid_route();
    route.mutation = MutationMode::HeadersOnly;
    route.compression = make_valid_compression();
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason, "compression requires mutation=full");
}

TEST(PolicySemanticValidatorTest, RejectsHeadersOnlyWithPagination) {
    auto route = make_minimal_valid_route();
    route.mutation = MutationMode::HeadersOnly;
    route.pagination = make_valid_pagination();
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason, "pagination requires mutation=full");
}

TEST(PolicySemanticValidatorTest, RejectsHeadersOnlyWithCacheStore) {
    auto route = make_minimal_valid_route();
    route.mutation = MutationMode::HeadersOnly;
    route.cache = make_valid_cache();
    route.cache.behavior = CacheBehavior::Store;
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason, "cache store requires mutation=full");
}

TEST(PolicySemanticValidatorTest, RejectsHeadersOnlyWithCoalescing) {
    auto route = make_minimal_valid_route();
    route.mutation = MutationMode::HeadersOnly;
    route.coalescing = make_valid_coalescing();
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_FALSE(validate_route_policy_semantic(route, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason, "coalescing requires mutation=full");
}

TEST(PolicySemanticValidatorTest, WarnsDisabledMutationWithFeatureBlocks) {
    auto route = make_minimal_valid_route();
    route.mutation = MutationMode::Disabled;
    route.compression = make_valid_compression(); // feature block active
    PolicyValidationResult result{};
    PolicyValidationOptions options{};

    EXPECT_TRUE(validate_route_policy_semantic(route, &result, options));
    EXPECT_TRUE(result.ok); // warning only
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].severity, PolicyValidationSeverity::Warning);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::Route);
    EXPECT_STREQ(result.issues[0].reason,
                 "feature block declared while mutation=disabled; observe-only behavior expected");
}

// ==========================================
// Document-level Tests
// ==========================================

TEST(PolicySemanticValidatorTest, RejectsDuplicateRouteId) {
    PolicyFileResult doc{};
    doc.count = 2;
    doc.policies[0] = make_minimal_valid_route();
    doc.policies[0].route_id = "r1";
    doc.policies[0].match_prefix = "/r1";
    doc.policies[1] = make_minimal_valid_route();
    doc.policies[1].route_id = "r1"; // duplicate ID
    doc.policies[1].match_prefix = "/r2";

    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = true;

    EXPECT_FALSE(validate_policy_file_semantic(doc, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::CrossRoute);
    EXPECT_STREQ(result.issues[0].reason, "duplicate route id");
}

TEST(PolicySemanticValidatorTest, RejectsDuplicateExactRouteWithSameMethod) {
    PolicyFileResult doc{};
    doc.count = 2;

    doc.policies[0] = make_minimal_valid_route();
    doc.policies[0].route_id = "r1";
    doc.policies[0].match_prefix = "/exact-path";
    doc.policies[0].match_kind = RouteMatchKind::Exact;
    doc.policies[0].allowed_method = HttpMethod::Get;

    doc.policies[1] = make_minimal_valid_route();
    doc.policies[1].route_id = "r2";
    doc.policies[1].match_prefix = "/exact-path";
    doc.policies[1].match_kind = RouteMatchKind::Exact;
    doc.policies[1].allowed_method = HttpMethod::Get; // duplicate path & method

    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = true;

    EXPECT_FALSE(validate_policy_file_semantic(doc, &result, options));
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::CrossRoute);
    EXPECT_STREQ(result.issues[0].reason, "duplicate exact route match");
}

TEST(PolicySemanticValidatorTest, WarnsPrefixShadowingNarrowerRoute) {
    PolicyFileResult doc{};
    doc.count = 2;

    doc.policies[0] = make_minimal_valid_route();
    doc.policies[0].route_id = "r1";
    doc.policies[0].match_prefix = "/api/v1/";
    doc.policies[0].match_kind = RouteMatchKind::Prefix;

    doc.policies[1] = make_minimal_valid_route();
    doc.policies[1].route_id = "r2";
    doc.policies[1].match_prefix = "/api/v1/cached/";
    doc.policies[1].match_kind = RouteMatchKind::Prefix; // shadowed by r1

    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = true;

    EXPECT_TRUE(validate_policy_file_semantic(doc, &result, options));
    EXPECT_TRUE(result.ok); // warnings only
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].severity, PolicyValidationSeverity::Warning);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::CrossRoute);
    EXPECT_STREQ(result.issues[0].reason, "route may be shadowed by earlier prefix route");
}

TEST(PolicySemanticValidatorTest, WarnsExactRouteShadowedByEarlierPrefix) {
    PolicyFileResult doc{};
    doc.count = 2;

    doc.policies[0] = make_minimal_valid_route();
    doc.policies[0].route_id = "r1";
    doc.policies[0].match_prefix = "/api/v1/";
    doc.policies[0].match_kind = RouteMatchKind::Prefix;

    doc.policies[1] = make_minimal_valid_route();
    doc.policies[1].route_id = "r2";
    doc.policies[1].match_prefix = "/api/v1/cached";
    doc.policies[1].match_kind = RouteMatchKind::Exact; // shadowed exact route

    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = true;

    EXPECT_TRUE(validate_policy_file_semantic(doc, &result, options));
    EXPECT_TRUE(result.ok); // warnings only
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].severity, PolicyValidationSeverity::Warning);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::CrossRoute);
    EXPECT_STREQ(result.issues[0].reason, "exact route may be shadowed by earlier prefix route");
}

TEST(PolicySemanticValidatorTest, WarnsSharedL2PathAcrossRoutes) {
    PolicyFileResult doc{};
    doc.count = 2;

    doc.policies[0] = make_minimal_valid_route();
    doc.policies[0].route_id = "r1";
    doc.policies[0].match_prefix = "/r1";
    doc.policies[0].cache = make_valid_cache();
    doc.policies[0].cache.l2.enabled = true;
    std::strcpy(doc.policies[0].cache.l2.path, "/tmp/shared_l2_rocksdb");

    doc.policies[1] = make_minimal_valid_route();
    doc.policies[1].route_id = "r2";
    doc.policies[1].match_prefix = "/r2";
    doc.policies[1].cache = make_valid_cache();
    doc.policies[1].cache.l2.enabled = true;
    std::strcpy(doc.policies[1].cache.l2.path, "/tmp/shared_l2_rocksdb"); // reuse same path

    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = true;

    EXPECT_TRUE(validate_policy_file_semantic(doc, &result, options));
    EXPECT_TRUE(result.ok); // warnings only
    ASSERT_EQ(result.issue_count, 1);
    EXPECT_EQ(result.issues[0].severity, PolicyValidationSeverity::Warning);
    EXPECT_EQ(result.issues[0].subsystem, PolicyValidationSubsystem::CrossRoute);
    EXPECT_STREQ(result.issues[0].reason, "multiple routes share the same L2 cache path");
}

TEST(PolicySemanticValidatorTest, CollectAllReturnsMultipleIssues) {
    PolicyFileResult doc{};
    doc.count = 2;

    doc.policies[0] = make_minimal_valid_route();
    doc.policies[0].route_id = nullptr; // Error 1

    doc.policies[1] = make_minimal_valid_route();
    doc.policies[1].route_id = "r2";
    doc.policies[1].match_prefix = "api/v1"; // Error 2

    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = true; // collect all

    EXPECT_FALSE(validate_policy_file_semantic(doc, &result, options));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.issue_count, 2);
}

TEST(PolicySemanticValidatorTest, FailFastStopsAtFirstError) {
    PolicyFileResult doc{};
    doc.count = 2;

    doc.policies[0] = make_minimal_valid_route();
    doc.policies[0].route_id = nullptr; // Error 1

    doc.policies[1] = make_minimal_valid_route();
    doc.policies[1].route_id = "r2";
    doc.policies[1].match_prefix = "api/v1"; // Error 2

    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = false; // fail-fast

    EXPECT_FALSE(validate_policy_file_semantic(doc, &result, options));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.issue_count, 1); // should halt immediately
}

} // namespace bytetaper::policy

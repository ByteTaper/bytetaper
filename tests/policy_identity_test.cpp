// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/policy_identity.h"
#include "policy/yaml_loader.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::policy {

namespace {

static RoutePolicy make_base_route() {
    RoutePolicy r{};
    r.route_id = "r1";
    r.match_prefix = "/api/v1";
    r.match_kind = RouteMatchKind::Prefix;
    r.mutation = MutationMode::Full;
    r.allowed_method = HttpMethod::Get;
    r.max_response_bytes = 1048576;
    r.failure_mode = FailureMode::FailOpen;

    r.field_filter.mode = FieldFilterMode::Allowlist;
    r.field_filter.field_count = 2;
    std::strcpy(r.field_filter.fields[0], "id");
    std::strcpy(r.field_filter.fields[1], "name");

    r.cache.enabled = true;
    r.cache.behavior = CacheBehavior::Store;
    r.cache.ttl_seconds = 300;
    r.cache.l1.enabled = true;
    r.cache.l1.capacity_entries = 1000;
    r.cache.l2.enabled = true;
    std::strcpy(r.cache.l2.path, "/var/cache/l2");
    r.cache.private_cache = true;
    std::strcpy(r.cache.auth_scope_header, "x-user-id");
    r.cache.field_variant.enabled = true;
    r.cache.field_variant.max_variants_per_route = 50;
    r.cache.field_variant.min_field_count = 1;
    r.cache.field_variant.max_field_count = 10;
    r.cache.field_variant.admission_threshold = 5;
    r.cache.field_variant.ttl_max_ms = 600000;
    r.cache.vary_headers.count = 1;
    std::strcpy(r.cache.vary_headers.names[0], "accept-language");

    r.pagination.enabled = true;
    r.pagination.mode = PaginationMode::LimitOffset;
    std::strcpy(r.pagination.limit_param, "limit");
    std::strcpy(r.pagination.offset_param, "offset");
    r.pagination.default_limit = 20;
    r.pagination.max_limit = 100;
    r.pagination.upstream_supports_pagination = true;
    r.pagination.max_response_bytes_warning = 524288;

    r.compression.enabled = true;
    r.compression.min_size_bytes = 1024;
    r.compression.eligible_content_type_count = 1;
    std::strcpy(r.compression.eligible_content_types[0], "application/json");
    r.compression.preferred_algorithm_count = 1;
    r.compression.preferred_algorithms[0] = CompressionAlgorithm::Gzip;
    r.compression.already_encoded_behavior = AlreadyEncodedBehavior::Skip;

    r.coalescing.enabled = true;
    r.coalescing.mode = CoalescingMode::CacheAssisted;
    r.coalescing.backend_timeout_ms = 5000;
    r.coalescing.handoff_buffer_ms = 100;
    r.coalescing.result_ready_retention_ms = 200;
    r.coalescing.max_waiters_per_key = 50;
    r.coalescing.require_cache_enabled = true;
    r.coalescing.allow_authenticated = false;
    r.coalescing.max_follower_wait_budget_ms = 1000;
    r.coalescing.max_active_follower_waiters = 200;
    r.coalescing.max_active_follower_waiters_per_shard = 50;

    return r;
}

} // namespace

TEST(PolicyIdentityV2Test, DeterminismAndPrefixFormat) {
    RoutePolicy r = make_base_route();
    PolicyIdentityV2 id1 = {};
    PolicyIdentityV2 id2 = {};

    ASSERT_TRUE(compute_route_policy_identity_v2(r, &id1));
    ASSERT_TRUE(compute_route_policy_identity_v2(r, &id2));

    EXPECT_STREQ(id1.value, id2.value);
    EXPECT_EQ(std::strlen(id1.value), 37u);
    EXPECT_EQ(std::strncmp(id1.value, "btp2:", 5), 0);
}

TEST(PolicyIdentityV2Test, CopyToLegacySlot) {
    RoutePolicy r = make_base_route();
    ASSERT_TRUE(copy_route_policy_identity_v2_to_legacy_slot(&r));

    EXPECT_EQ(std::strlen(r.policy_identity), 37u);
    EXPECT_EQ(std::strncmp(r.policy_identity, "btp2:", 5), 0);
}

TEST(PolicyIdentityV2Test, PointerAddressIndependence) {
    RoutePolicy r1 = make_base_route();
    RoutePolicy r2 = make_base_route();

    // Allocate distinct memory buffers for pointer string fields containing identical text
    char buf_id1[] = "r1_custom_id";
    char buf_id2[] = "r1_custom_id";
    char buf_prefix1[] = "/api/v1_custom_prefix";
    char buf_prefix2[] = "/api/v1_custom_prefix";

    // Verify separate backing memory addresses
    ASSERT_NE(static_cast<void*>(buf_id1), static_cast<void*>(buf_id2));
    ASSERT_NE(static_cast<void*>(buf_prefix1), static_cast<void*>(buf_prefix2));

    r1.route_id = buf_id1;
    r1.match_prefix = buf_prefix1;

    r2.route_id = buf_id2;
    r2.match_prefix = buf_prefix2;

    PolicyIdentityV2 id1 = {};
    PolicyIdentityV2 id2 = {};

    ASSERT_TRUE(compute_route_policy_identity_v2(r1, &id1));
    ASSERT_TRUE(compute_route_policy_identity_v2(r2, &id2));

    EXPECT_STREQ(id1.value, id2.value);
}

TEST(PolicyIdentityV2Test, PaddingAndUnusedStorageIndependence) {
    RoutePolicy r1;
    RoutePolicy r2;

    std::memset(&r1, 0x55, sizeof(RoutePolicy));
    std::memset(&r2, 0xAA, sizeof(RoutePolicy));

    // Re-initialize semantic fields identically
    r1 = make_base_route();
    r2 = make_base_route();

    PolicyIdentityV2 id1 = {};
    PolicyIdentityV2 id2 = {};

    ASSERT_TRUE(compute_route_policy_identity_v2(r1, &id1));
    ASSERT_TRUE(compute_route_policy_identity_v2(r2, &id2));

    EXPECT_STREQ(id1.value, id2.value);
}

TEST(PolicyIdentityV2Test, NullptrDefense) {
    RoutePolicy r = make_base_route();
    EXPECT_FALSE(compute_route_policy_identity_v2(r, nullptr));
    EXPECT_FALSE(compute_policy_file_identity_v2(&r, 1, nullptr));
    EXPECT_FALSE(compute_policy_file_identity_v2(nullptr, 1, nullptr));

    PolicyIdentityV2 out = {};
    EXPECT_FALSE(compute_policy_file_identity_v2(nullptr, 1, &out));
}

TEST(PolicyIdentityV2Test, EmptyDocumentIdentity) {
    PolicyIdentityV2 out = {};
    ASSERT_TRUE(compute_policy_file_identity_v2(nullptr, 0, &out));
    EXPECT_EQ(std::strncmp(out.value, "btp2-doc:", 9), 0);
    EXPECT_EQ(std::strlen(out.value), 41u);
}

TEST(PolicyIdentityV2Test, CollectionOrderingSensitivity) {
    // 1. Field filter order
    {
        RoutePolicy r1 = make_base_route();
        RoutePolicy r2 = make_base_route();

        r1.field_filter.field_count = 2;
        std::strcpy(r1.field_filter.fields[0], "id");
        std::strcpy(r1.field_filter.fields[1], "name");

        r2.field_filter.field_count = 2;
        std::strcpy(r2.field_filter.fields[0], "name");
        std::strcpy(r2.field_filter.fields[1], "id");

        PolicyIdentityV2 id1 = {};
        PolicyIdentityV2 id2 = {};
        ASSERT_TRUE(compute_route_policy_identity_v2(r1, &id1));
        ASSERT_TRUE(compute_route_policy_identity_v2(r2, &id2));
        EXPECT_STRNE(id1.value, id2.value);
    }

    // 2. Cache vary headers order
    {
        RoutePolicy r1 = make_base_route();
        RoutePolicy r2 = make_base_route();

        r1.cache.vary_headers.count = 2;
        std::strcpy(r1.cache.vary_headers.names[0], "accept-encoding");
        std::strcpy(r1.cache.vary_headers.names[1], "accept-language");

        r2.cache.vary_headers.count = 2;
        std::strcpy(r2.cache.vary_headers.names[0], "accept-language");
        std::strcpy(r2.cache.vary_headers.names[1], "accept-encoding");

        PolicyIdentityV2 id1 = {};
        PolicyIdentityV2 id2 = {};
        ASSERT_TRUE(compute_route_policy_identity_v2(r1, &id1));
        ASSERT_TRUE(compute_route_policy_identity_v2(r2, &id2));
        EXPECT_STRNE(id1.value, id2.value);
    }

    // 3. Compression preferred algorithms order
    {
        RoutePolicy r1 = make_base_route();
        RoutePolicy r2 = make_base_route();

        r1.compression.preferred_algorithm_count = 2;
        r1.compression.preferred_algorithms[0] = CompressionAlgorithm::Gzip;
        r1.compression.preferred_algorithms[1] = CompressionAlgorithm::Brotli;

        r2.compression.preferred_algorithm_count = 2;
        r2.compression.preferred_algorithms[0] = CompressionAlgorithm::Brotli;
        r2.compression.preferred_algorithms[1] = CompressionAlgorithm::Gzip;

        PolicyIdentityV2 id1 = {};
        PolicyIdentityV2 id2 = {};
        ASSERT_TRUE(compute_route_policy_identity_v2(r1, &id1));
        ASSERT_TRUE(compute_route_policy_identity_v2(r2, &id2));
        EXPECT_STRNE(id1.value, id2.value);
    }
}

TEST(PolicyIdentityV2Test, DocumentSensitivityRouteChanges) {
    RoutePolicy routes[3];
    routes[0] = make_base_route();
    routes[0].route_id = "r1";
    routes[1] = make_base_route();
    routes[1].route_id = "r2";
    routes[2] = make_base_route();
    routes[2].route_id = "r3";

    PolicyIdentityV2 base_doc = {};
    ASSERT_TRUE(compute_policy_file_identity_v2(routes, 2, &base_doc));

    // 1. Added route
    PolicyIdentityV2 added_doc = {};
    ASSERT_TRUE(compute_policy_file_identity_v2(routes, 3, &added_doc));
    EXPECT_STRNE(base_doc.value, added_doc.value);

    // 2. Removed route
    PolicyIdentityV2 removed_doc = {};
    ASSERT_TRUE(compute_policy_file_identity_v2(routes, 1, &removed_doc));
    EXPECT_STRNE(base_doc.value, removed_doc.value);

    // 3. Semantically changed route
    RoutePolicy modified_routes[2];
    modified_routes[0] = routes[0];
    modified_routes[1] = routes[1];
    modified_routes[1].cache.ttl_seconds = 999; // mutate semantic field

    PolicyIdentityV2 modified_doc = {};
    ASSERT_TRUE(compute_policy_file_identity_v2(modified_routes, 2, &modified_doc));
    EXPECT_STRNE(base_doc.value, modified_doc.value);
}

TEST(PolicyIdentityV2Test, DocumentIdentityStableForSameRoutesSameOrder) {
    RoutePolicy routes[2];
    routes[0] = make_base_route();
    routes[0].route_id = "r1";
    routes[1] = make_base_route();
    routes[1].route_id = "r2";

    PolicyIdentityV2 doc_id1 = {};
    PolicyIdentityV2 doc_id2 = {};

    ASSERT_TRUE(compute_policy_file_identity_v2(routes, 2, &doc_id1));
    ASSERT_TRUE(compute_policy_file_identity_v2(routes, 2, &doc_id2));

    EXPECT_STREQ(doc_id1.value, doc_id2.value);
    EXPECT_EQ(std::strncmp(doc_id1.value, "btp2-doc:", 9), 0);
}

TEST(PolicyIdentityV2Test, DocumentIdentityChangesWhenRouteOrderChanges) {
    RoutePolicy routes_ab[2];
    routes_ab[0] = make_base_route();
    routes_ab[0].route_id = "r1";
    routes_ab[1] = make_base_route();
    routes_ab[1].route_id = "r2";

    RoutePolicy routes_ba[2];
    routes_ba[0] = routes_ab[1];
    routes_ba[1] = routes_ab[0];

    PolicyIdentityV2 doc_id_ab = {};
    PolicyIdentityV2 doc_id_ba = {};

    ASSERT_TRUE(compute_policy_file_identity_v2(routes_ab, 2, &doc_id_ab));
    ASSERT_TRUE(compute_policy_file_identity_v2(routes_ba, 2, &doc_id_ba));

    EXPECT_STRNE(doc_id_ab.value, doc_id_ba.value);
}

TEST(PolicyIdentityV2Test, YamlLoaderIdentityStabilityAndDifferentiator) {
    const std::string yaml_base = R"(
routes:
  - id: api-v1-proxy
    match:
      kind: prefix
      prefix: /api/v1/
    mutation: full
    method: get
    max_response_bytes: 2097152
)";

    const std::string yaml_with_formatting = R"(
# A comment in the policy file
routes:
  - id: "api-v1-proxy"
    match:
      kind:   "prefix"
      prefix:   /api/v1/   # Trailing inline comment
    mutation:   full
    method:     get
    max_response_bytes: 2097152
)";

    const std::string yaml_different_field = R"(
routes:
  - id: api-v1-proxy
    match:
      kind: prefix
      prefix: /api/v1/
    mutation: full
    method: get
    max_response_bytes: 999999
)";

    // 1. Loading same yaml content multiple times is stable
    PolicyFileResult r1 = {};
    PolicyFileResult r2 = {};
    ASSERT_TRUE(load_policy_from_string(yaml_base.c_str(), &r1));
    ASSERT_TRUE(load_policy_from_string(yaml_base.c_str(), &r2));
    EXPECT_STREQ(r1.policies[0].policy_identity, r2.policies[0].policy_identity);

    // 2. Formatting and comments do not change identity
    PolicyFileResult r_fmt = {};
    ASSERT_TRUE(load_policy_from_string(yaml_with_formatting.c_str(), &r_fmt));
    EXPECT_STREQ(r1.policies[0].policy_identity, r_fmt.policies[0].policy_identity);

    // 3. Semantically different fields change identity
    PolicyFileResult r_diff = {};
    ASSERT_TRUE(load_policy_from_string(yaml_different_field.c_str(), &r_diff));
    EXPECT_STRNE(r1.policies[0].policy_identity, r_diff.policies[0].policy_identity);
}

// Exhaustive domain-level field sensitivity verification
class PolicyIdentityV2FieldSensitivityTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_route = make_base_route();
        ASSERT_TRUE(compute_route_policy_identity_v2(base_route, &base_id));
    }

    void AssertChanged(const RoutePolicy& mutated) {
        PolicyIdentityV2 mut_id = {};
        ASSERT_TRUE(compute_route_policy_identity_v2(mutated, &mut_id));
        EXPECT_STRNE(base_id.value, mut_id.value) << "Hash did not change on mutation!";
    }

    RoutePolicy base_route;
    PolicyIdentityV2 base_id;
};

// Route base fields
TEST_F(PolicyIdentityV2FieldSensitivityTest, RouteId) {
    RoutePolicy m = base_route;
    m.route_id = "r2";
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, MatchPrefix) {
    RoutePolicy m = base_route;
    m.match_prefix = "/api/v2";
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, MatchKind) {
    RoutePolicy m = base_route;
    m.match_kind = RouteMatchKind::Exact;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, MutationMode) {
    RoutePolicy m = base_route;
    m.mutation = MutationMode::HeadersOnly;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, AllowedMethod) {
    RoutePolicy m = base_route;
    m.allowed_method = HttpMethod::Post;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, MaxResponseBytes) {
    RoutePolicy m = base_route;
    m.max_response_bytes = 2097152;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, FailureMode) {
    RoutePolicy m = base_route;
    m.failure_mode = FailureMode::FailClosed;
    AssertChanged(m);
}

// Field filter fields
TEST_F(PolicyIdentityV2FieldSensitivityTest, FieldFilterMode) {
    RoutePolicy m = base_route;
    m.field_filter.mode = FieldFilterMode::Denylist;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, FieldFilterFieldCount) {
    RoutePolicy m = base_route;
    m.field_filter.field_count = 1;
    AssertChanged(m);
}

// Change elements inside arrays
TEST_F(PolicyIdentityV2FieldSensitivityTest, FieldFilterIndividualField) {
    RoutePolicy m = base_route;
    std::strcpy(m.field_filter.fields[0], "userid");
    AssertChanged(m);
}

// Cache fields
TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheEnabled) {
    RoutePolicy m = base_route;
    m.cache.enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheBehavior) {
    RoutePolicy m = base_route;
    m.cache.behavior = CacheBehavior::Bypass;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheTtl) {
    RoutePolicy m = base_route;
    m.cache.ttl_seconds = 600;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheL1Enabled) {
    RoutePolicy m = base_route;
    m.cache.l1.enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheL1Capacity) {
    RoutePolicy m = base_route;
    m.cache.l1.capacity_entries = 5000;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheL2Enabled) {
    RoutePolicy m = base_route;
    m.cache.l2.enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheL2Path) {
    RoutePolicy m = base_route;
    std::strcpy(m.cache.l2.path, "/tmp/cache");
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CachePrivate) {
    RoutePolicy m = base_route;
    m.cache.private_cache = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheAuthScopeHeader) {
    RoutePolicy m = base_route;
    std::strcpy(m.cache.auth_scope_header, "authorization");
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheFieldVariantEnabled) {
    RoutePolicy m = base_route;
    m.cache.field_variant.enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheFieldVariantMaxVariants) {
    RoutePolicy m = base_route;
    m.cache.field_variant.max_variants_per_route = 100;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheFieldVariantMinFieldCount) {
    RoutePolicy m = base_route;
    m.cache.field_variant.min_field_count = 2;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheFieldVariantMaxFieldCount) {
    RoutePolicy m = base_route;
    m.cache.field_variant.max_field_count = 20;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheFieldVariantAdmissionThreshold) {
    RoutePolicy m = base_route;
    m.cache.field_variant.admission_threshold = 10;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheFieldVariantTtlMax) {
    RoutePolicy m = base_route;
    m.cache.field_variant.ttl_max_ms = 1200000;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheVaryHeadersCount) {
    RoutePolicy m = base_route;
    m.cache.vary_headers.count = 0;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CacheVaryHeadersIndividualName) {
    RoutePolicy m = base_route;
    std::strcpy(m.cache.vary_headers.names[0], "user-agent");
    AssertChanged(m);
}

// Pagination fields
TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationEnabled) {
    RoutePolicy m = base_route;
    m.pagination.enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationMode) {
    RoutePolicy m = base_route;
    m.pagination.mode = PaginationMode::Cursor;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationLimitParam) {
    RoutePolicy m = base_route;
    std::strcpy(m.pagination.limit_param, "size");
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationOffsetParam) {
    RoutePolicy m = base_route;
    std::strcpy(m.pagination.offset_param, "start");
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationDefaultLimit) {
    RoutePolicy m = base_route;
    m.pagination.default_limit = 50;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationMaxLimit) {
    RoutePolicy m = base_route;
    m.pagination.max_limit = 500;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationUpstreamSupports) {
    RoutePolicy m = base_route;
    m.pagination.upstream_supports_pagination = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, PaginationMaxBytesWarning) {
    RoutePolicy m = base_route;
    m.pagination.max_response_bytes_warning = 1048576;
    AssertChanged(m);
}

// Compression fields
TEST_F(PolicyIdentityV2FieldSensitivityTest, CompressionEnabled) {
    RoutePolicy m = base_route;
    m.compression.enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CompressionMinSizeBytes) {
    RoutePolicy m = base_route;
    m.compression.min_size_bytes = 2048;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CompressionEligibleContentTypeCount) {
    RoutePolicy m = base_route;
    m.compression.eligible_content_type_count = 0;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CompressionEligibleIndividualContentType) {
    RoutePolicy m = base_route;
    std::strcpy(m.compression.eligible_content_types[0], "text/html");
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CompressionPreferredAlgorithmCount) {
    RoutePolicy m = base_route;
    m.compression.preferred_algorithm_count = 0;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CompressionPreferredIndividualAlgorithm) {
    RoutePolicy m = base_route;
    m.compression.preferred_algorithms[0] = CompressionAlgorithm::Brotli;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CompressionAlreadyEncodedBehavior) {
    RoutePolicy m = base_route;
    m.compression.already_encoded_behavior = AlreadyEncodedBehavior::Passthrough;
    AssertChanged(m);
}

// Coalescing fields
TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingEnabled) {
    RoutePolicy m = base_route;
    m.coalescing.enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingMode) {
    RoutePolicy m = base_route;
    m.coalescing.mode = static_cast<CoalescingMode>(99);
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingBackendTimeout) {
    RoutePolicy m = base_route;
    m.coalescing.backend_timeout_ms = 10000;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingHandoffBuffer) {
    RoutePolicy m = base_route;
    m.coalescing.handoff_buffer_ms = 50;
    AssertChanged(m);
}

// Document level identity tests
TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingResultReadyRetention) {
    RoutePolicy m = base_route;
    m.coalescing.result_ready_retention_ms = 500;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingMaxWaiters) {
    RoutePolicy m = base_route;
    m.coalescing.max_waiters_per_key = 100;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingRequireCache) {
    RoutePolicy m = base_route;
    m.coalescing.require_cache_enabled = false;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingAllowAuthenticated) {
    RoutePolicy m = base_route;
    m.coalescing.allow_authenticated = true;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingMaxFollowerWaitBudget) {
    RoutePolicy m = base_route;
    m.coalescing.max_follower_wait_budget_ms = 2000;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingMaxActiveFollowers) {
    RoutePolicy m = base_route;
    m.coalescing.max_active_follower_waiters = 500;
    AssertChanged(m);
}

TEST_F(PolicyIdentityV2FieldSensitivityTest, CoalescingMaxActiveFollowersPerShard) {
    RoutePolicy m = base_route;
    m.coalescing.max_active_follower_waiters_per_shard = 100;
    AssertChanged(m);
}

} // namespace bytetaper::policy

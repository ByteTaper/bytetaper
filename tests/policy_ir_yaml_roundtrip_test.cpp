// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_from_yaml.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_roundtrip.h"

#include <gtest/gtest.h>

namespace bytetaper::taperquery {

namespace {

TqPolicyDocument create_test_policy() {
    TqPolicyDocument doc;
    doc.document_id = "test-doc-id";
    doc.expected_base_sha = "abc123expectedbasesha";
    doc.schema_version = "yaml/v1";
    doc.version.source_schema_version = "yaml/v1";

    TqRoutePolicy route;
    route.route_id = "route-1";
    route.match_prefix = "/api/v1/users";
    route.match_kind = TqRouteMatchKind::Prefix;
    route.mutation = TqMutationMode::Disabled;
    route.allowed_method = TqHttpMethod::Get;

    // field filter
    route.field_filter.mode = TqFieldFilterMode::Allowlist;
    route.field_filter.fields = { "id", "name", "email" };

    // cache
    route.cache.enabled = true;
    route.cache.behavior = TqCacheBehavior::Store;
    route.cache.ttl_ms = 60000;
    route.cache.l1.enabled = true;
    route.cache.l1.capacity_entries = 1000;

    doc.routes.push_back(route);
    return doc;
}

} // namespace

TEST(PolicyIrYamlRoundTripTest, RoundTripPreservesIdentity) {
    TqPolicyDocument doc = create_test_policy();
    auto result = emit_and_reparse_canonical_policy_yaml(doc);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_FALSE(result.canonical_yaml.empty());
    EXPECT_EQ(result.candidate_policy_identity, result.persisted_policy_identity);
}

TEST(PolicyIrYamlRoundTripTest, RoundTripParsedIrMatchesOriginal) {
    TqPolicyDocument doc = create_test_policy();
    auto result = emit_and_reparse_canonical_policy_yaml(doc);
    ASSERT_TRUE(result.ok) << result.error;

    std::string computed = compute_policy_document_identity(result.parsed_policy_ir);
    EXPECT_EQ(computed, result.candidate_policy_identity);
    EXPECT_EQ(result.parsed_policy_ir.document_id, doc.document_id);
    EXPECT_EQ(result.parsed_policy_ir.expected_base_sha, doc.expected_base_sha);
}

TEST(PolicyIrYamlRoundTripTest, RoundTripYamlIsReloadable) {
    TqPolicyDocument doc = create_test_policy();
    auto result = emit_and_reparse_canonical_policy_yaml(doc);
    ASSERT_TRUE(result.ok) << result.error;

    auto reload = load_policy_ir_from_yaml_string(result.canonical_yaml.c_str(),
                                                  result.canonical_yaml.size());
    ASSERT_TRUE(reload.ok) << reload.error;
    EXPECT_EQ(reload.policy.document_id, doc.document_id);
}

TEST(PolicyIrYamlRoundTripTest, DifferentPoliciesProduceDifferentIdentities) {
    TqPolicyDocument doc1 = create_test_policy();

    TqPolicyDocument doc2 = create_test_policy();
    doc2.routes[0].route_id = "route-different";

    auto result1 = emit_and_reparse_canonical_policy_yaml(doc1);
    auto result2 = emit_and_reparse_canonical_policy_yaml(doc2);

    ASSERT_TRUE(result1.ok) << result1.error;
    ASSERT_TRUE(result2.ok) << result2.error;
    EXPECT_NE(result1.persisted_policy_identity, result2.persisted_policy_identity);
}

} // namespace bytetaper::taperquery

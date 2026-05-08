// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/cache_safety.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::cache {

static CacheKeyInput make_basic_input() {
    CacheKeyInput input{};
    input.method = policy::HttpMethod::Get;
    input.route_id = "api-v1";
    input.path = "/api/items";
    input.query = nullptr;
    input.selected_fields = nullptr;
    input.selected_field_count = 0;
    input.policy_version = "v1";
    return input;
}

TEST(CacheKeyTest, SameInputSameKey) {
    char key1[512] = {};
    char key2[512] = {};
    CacheKeyInput input = make_basic_input();
    EXPECT_TRUE(build_cache_key(input, key1, sizeof(key1)));
    EXPECT_TRUE(build_cache_key(input, key2, sizeof(key2)));
    EXPECT_STREQ(key1, key2);
}

TEST(CacheKeyTest, DifferentFieldsDifferentKey) {
    char key1[512] = {};
    char key2[512] = {};

    char fields1[1][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields1[0], "id", policy::kMaxFieldNameLen - 1);

    char fields2[2][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields2[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields2[1], "name", policy::kMaxFieldNameLen - 1);

    CacheKeyInput input = make_basic_input();

    input.selected_fields = fields1;
    input.selected_field_count = 1;
    EXPECT_TRUE(build_cache_key(input, key1, sizeof(key1)));

    input.selected_fields = fields2;
    input.selected_field_count = 2;
    EXPECT_TRUE(build_cache_key(input, key2, sizeof(key2)));

    EXPECT_NE(std::strcmp(key1, key2), 0);
}

TEST(CacheKeyTest, DifferentPolicyVersionDifferentKey) {
    char key1[512] = {};
    char key2[512] = {};
    CacheKeyInput input = make_basic_input();

    input.policy_version = "v1";
    EXPECT_TRUE(build_cache_key(input, key1, sizeof(key1)));

    input.policy_version = "v2";
    EXPECT_TRUE(build_cache_key(input, key2, sizeof(key2)));

    EXPECT_NE(std::strcmp(key1, key2), 0);
}

TEST(CacheKeyTest, NonGetReturnsFalse) {
    char key[512] = {};
    CacheKeyInput input = make_basic_input();
    input.method = policy::HttpMethod::Post;
    EXPECT_FALSE(build_cache_key(input, key, sizeof(key)));
}

TEST(CacheKeyTest, FieldOrderIndependence) {
    char key1[512] = {};
    char key2[512] = {};

    char fields_ab[2][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields_ab[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields_ab[1], "name", policy::kMaxFieldNameLen - 1);

    char fields_ba[2][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields_ba[0], "name", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields_ba[1], "id", policy::kMaxFieldNameLen - 1);

    CacheKeyInput input = make_basic_input();

    input.selected_fields = fields_ab;
    input.selected_field_count = 2;
    EXPECT_TRUE(build_cache_key(input, key1, sizeof(key1)));

    input.selected_fields = fields_ba;
    input.selected_field_count = 2;
    EXPECT_TRUE(build_cache_key(input, key2, sizeof(key2)));

    EXPECT_STREQ(key1, key2);
}

TEST(CacheKeyTest, EmptyFieldsAndQueryStillValid) {
    char key[512] = {};
    CacheKeyInput input = make_basic_input();
    input.query = nullptr;
    input.selected_fields = nullptr;
    input.selected_field_count = 0;
    EXPECT_TRUE(build_cache_key(input, key, sizeof(key)));
    EXPECT_NE(key[0], '\0');
}

TEST(CacheKeyTest, FieldDeduplication) {
    char key1[512] = {};
    char key2[512] = {};

    char fields_dup[3][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields_dup[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields_dup[1], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields_dup[2], "name", policy::kMaxFieldNameLen - 1);

    char fields_unique[2][policy::kMaxFieldNameLen] = {};
    std::strncpy(fields_unique[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(fields_unique[1], "name", policy::kMaxFieldNameLen - 1);

    CacheKeyInput input = make_basic_input();

    input.selected_fields = fields_dup;
    input.selected_field_count = 3;
    EXPECT_TRUE(build_cache_key(input, key1, sizeof(key1)));

    input.selected_fields = fields_unique;
    input.selected_field_count = 2;
    EXPECT_TRUE(build_cache_key(input, key2, sizeof(key2)));

    EXPECT_STREQ(key1, key2);
}

TEST(CacheKeyTest, VariantPrefixEmitted) {
    char key_raw[512] = {};
    char key_var[512] = {};

    CacheKeyInput input = make_basic_input();
    EXPECT_TRUE(build_cache_key(input, key_raw, sizeof(key_raw)));

    input.variant = true;
    EXPECT_TRUE(build_cache_key(input, key_var, sizeof(key_var)));

    // key_var should be "var:" + key_raw
    char expected[520];
    std::snprintf(expected, sizeof(expected), "var:%s", key_raw);
    EXPECT_STREQ(key_var, expected);
}

TEST(CacheKeyTest, SanitizeQueryStripFieldsParam) {
    char out[128] = {};

    // 1. Only fields
    EXPECT_EQ(sanitize_query_strip_fields_param("fields=id,name", out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "");

    EXPECT_EQ(sanitize_query_strip_fields_param("?fields=id,name", out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "");

    // 2. Middle fields
    std::memset(out, 0, sizeof(out));
    EXPECT_GT(
        sanitize_query_strip_fields_param("?foo=bar&fields=id,name&baz=qux", out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "?foo=bar&baz=qux");

    // 3. Leading fields
    std::memset(out, 0, sizeof(out));
    EXPECT_GT(sanitize_query_strip_fields_param("?fields=id,name&foo=bar", out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "?foo=bar");

    // 4. Trailing fields
    std::memset(out, 0, sizeof(out));
    EXPECT_GT(sanitize_query_strip_fields_param("?foo=bar&fields=id,name", out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "?foo=bar");

    // 5. No fields
    std::memset(out, 0, sizeof(out));
    EXPECT_GT(sanitize_query_strip_fields_param("?foo=bar", out, sizeof(out)), 0u);
    EXPECT_STREQ(out, "?foo=bar");

    // 6. Null input
    EXPECT_EQ(sanitize_query_strip_fields_param(nullptr, out, sizeof(out)), 0u);
}

TEST(CacheKeyTest, VaryHeader_DifferentValues_DifferentKeys) {
    char key1[512] = {};
    char key2[512] = {};
    CacheKeyInput input = make_basic_input();

    input.vary_headers[0] = { "accept-language", "abcd1234" };
    input.vary_header_count = 1;
    EXPECT_TRUE(build_cache_key(input, key1, sizeof(key1)));

    input.vary_headers[0] = { "accept-language", "5678efgh" };
    EXPECT_TRUE(build_cache_key(input, key2, sizeof(key2)));

    EXPECT_NE(std::strcmp(key1, key2), 0);
    EXPECT_NE(std::strstr(key1, "vary:accept-language=abcd1234"), nullptr);
    EXPECT_NE(std::strstr(key2, "vary:accept-language=5678efgh"), nullptr);
}

TEST(CacheKeyTest, VaryHeader_SameValues_SameKeys) {
    char key1[512] = {};
    char key2[512] = {};
    CacheKeyInput input = make_basic_input();

    input.vary_headers[0] = { "accept-language", "abcd1234" };
    input.vary_header_count = 1;
    EXPECT_TRUE(build_cache_key(input, key1, sizeof(key1)));
    EXPECT_TRUE(build_cache_key(input, key2, sizeof(key2)));

    EXPECT_STREQ(key1, key2);
}

TEST(CacheKeyTest, VaryHeader_NoHeadersConfigured_NoSegment) {
    char key[512] = {};
    CacheKeyInput input = make_basic_input();
    input.vary_header_count = 0;
    EXPECT_TRUE(build_cache_key(input, key, sizeof(key)));
    EXPECT_EQ(std::strstr(key, "vary:"), nullptr);
}

TEST(CacheKeyTest, VaryHeader_MultipleHeaders_AllIncluded) {
    char key[512] = {};
    CacheKeyInput input = make_basic_input();

    input.vary_headers[0] = { "accept-language", "abcd1234" };
    input.vary_headers[1] = { "x-api-version", "9999" };
    input.vary_header_count = 2;
    EXPECT_TRUE(build_cache_key(input, key, sizeof(key)));

    EXPECT_NE(std::strstr(key, "vary:accept-language=abcd1234,x-api-version=9999"), nullptr);
}

TEST(CacheKeyTest, VaryHeader_EmptyValueSentinelDiffersFromMissingSentinel) {
    // Proves the two sentinels used by prepare_cache_vary_context() hash differently.
    char h_missing[17] = {};
    char h_empty[17] = {};
    EXPECT_TRUE(build_cache_vary_value_hash("<missing>", 9, h_missing, sizeof(h_missing)));
    EXPECT_TRUE(build_cache_vary_value_hash("<empty>", 7, h_empty, sizeof(h_empty)));
    EXPECT_STRNE(h_missing, h_empty);
}

} // namespace bytetaper::cache

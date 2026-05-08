// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "policy/route_policy.h"
#include "stages/cache_key_prepare_stage.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::stages {

class CacheKeyPrepareStageTest : public ::testing::Test {
protected:
    apg::ApgTransformContext context{};
    policy::RoutePolicy policy{};

    void SetUp() override {
        context.matched_policy = &policy;
        policy.route_id = "test-route";
        policy.cache.behavior = policy::CacheBehavior::Store;
        context.request_method = policy::HttpMethod::Get;
        std::strncpy(context.raw_path, "/api/test", sizeof(context.raw_path) - 1);
    }
};

TEST_F(CacheKeyPrepareStageTest, PrepareStage_CacheEligible) {
    auto result = cache_key_prepare_stage(context);
    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_TRUE(context.cache_eligible);
    EXPECT_TRUE(context.cache_key_ready);
    EXPECT_NE(context.cache_key[0], '\0');
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_NoPolicySkips) {
    context.matched_policy = nullptr;
    auto result = cache_key_prepare_stage(context);
    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_FALSE(context.cache_eligible);
    EXPECT_FALSE(context.cache_key_ready);
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_CacheDisabledSkips) {
    policy.cache.behavior = policy::CacheBehavior::Bypass;
    auto result = cache_key_prepare_stage(context);
    EXPECT_FALSE(context.cache_eligible);
    EXPECT_FALSE(context.cache_key_ready);
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_NonGetSkips) {
    context.request_method = policy::HttpMethod::Post;
    auto result = cache_key_prepare_stage(context);
    EXPECT_FALSE(context.cache_eligible);
    EXPECT_FALSE(context.cache_key_ready);
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_KeyIsDeterministic) {
    cache_key_prepare_stage(context);
    char first_key[cache::kCacheKeyMaxLen];
    std::memcpy(first_key, context.cache_key, cache::kCacheKeyMaxLen);

    // Run again
    cache_key_prepare_stage(context);
    EXPECT_STREQ(first_key, context.cache_key);
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_SharedKeyProof) {
    cache_key_prepare_stage(context);

    // Build key directly using the same logic (raw key uses sanitized query and 0 fields)
    cache::CacheKeyInput ki{};
    ki.method = context.request_method;
    ki.route_id = context.matched_policy->route_id;
    ki.path = context.raw_path;
    ki.query = context.sanitized_query_ready ? context.sanitized_query : context.raw_query;
    ki.selected_fields = nullptr;
    ki.selected_field_count = 0;
    ki.policy_version = context.matched_policy->route_id;
    ki.variant = false;

    char expected_key[cache::kCacheKeyMaxLen] = {};
    cache::build_cache_key(ki, expected_key, sizeof(expected_key));

    EXPECT_STREQ(expected_key, context.cache_key);
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_AlreadyReadyIsIdempotent) {
    cache_key_prepare_stage(context);
    EXPECT_TRUE(context.cache_key_ready);

    auto result = cache_key_prepare_stage(context);
    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_TRUE(context.cache_key_ready);
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_VariantKeyBuiltWhenPolicyEnabled) {
    policy.cache.field_variant.enabled = true;
    std::strncpy(context.selected_fields[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(context.selected_fields[1], "name", policy::kMaxFieldNameLen - 1);
    context.selected_field_count = 2;

    auto result = cache_key_prepare_stage(context);
    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_TRUE(context.cache_key_ready);
    EXPECT_TRUE(context.variant_cache_key_ready);
    EXPECT_NE(context.variant_cache_key[0], '\0');

    // Variant key must have "var:" prefix
    EXPECT_EQ(std::strncmp(context.variant_cache_key, "var:", 4), 0);
}

TEST_F(CacheKeyPrepareStageTest, PrepareStage_QuerySanitationAndVariantKey) {
    policy.cache.field_variant.enabled = true;
    std::strncpy(context.raw_query, "?fields=id,name&foo=bar", sizeof(context.raw_query) - 1);
    std::strncpy(context.selected_fields[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(context.selected_fields[1], "name", policy::kMaxFieldNameLen - 1);
    context.selected_field_count = 2;

    cache_key_prepare_stage(context);
    EXPECT_TRUE(context.sanitized_query_ready);
    EXPECT_STREQ(context.sanitized_query, "?foo=bar");

    // Raw key should have stripped query, no fields segment, and no var prefix
    EXPECT_NE(std::strncmp(context.cache_key, "var:", 4), 0); // should not start with var:
    EXPECT_TRUE(std::strstr(context.cache_key, "foo=bar") != nullptr);
    EXPECT_TRUE(std::strstr(context.cache_key, "fields=") == nullptr);

    // Variant key should have stripped query, var prefix
    EXPECT_EQ(std::strncmp(context.variant_cache_key, "var:", 4), 0);
    EXPECT_TRUE(std::strstr(context.variant_cache_key, "foo=bar") != nullptr);
    EXPECT_TRUE(std::strstr(context.variant_cache_key, "fields=") == nullptr);
}

} // namespace bytetaper::stages

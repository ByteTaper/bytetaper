// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"
#include "policy/route_policy.h"
#include "stages/l1_variant_lookup_stage.h"
#include "stages/l1_variant_store_stage.h"

#include <cstring>
#include <gtest/gtest.h>
#include <memory>

namespace bytetaper::stages {

class L1VariantStageTest : public ::testing::Test {
protected:
    apg::ApgTransformContext context{};
    policy::RoutePolicy policy{};
    std::unique_ptr<cache::L1Cache> l1;

    void SetUp() override {
        l1 = std::make_unique<cache::L1Cache>();
        cache::l1_init(l1.get());

        context.matched_policy = &policy;
        policy.route_id = "test-route";
        policy.cache.behavior = policy::CacheBehavior::Store;
        policy.cache.ttl_seconds = 10;
        policy.cache.field_variant.enabled = true;
        policy.cache.field_variant.max_variants_per_route = 5;
        policy.cache.field_variant.ttl_max_ms = 5000; // 5 seconds maximum variant TTL

        context.l1_cache = l1.get();
        context.request_method = policy::HttpMethod::Get;
        context.response_status_code = 200;
        std::strncpy(context.response_content_type, "application/json",
                     sizeof(context.response_content_type) - 1);
        context.request_epoch_ms = 1000000;

        context.variant_cache_key_ready = true;
        std::strncpy(context.variant_cache_key, "var:GET|r1|/path||version|f1,f2",
                     sizeof(context.variant_cache_key) - 1);

        context.response_body = "{\"f1\":1,\"f2\":2}";
        context.response_body_len = std::strlen(context.response_body);
        context.variant_admission_passed = true;
    }
};

TEST_F(L1VariantStageTest, Lookup_EnforcesPreconditions) {
    // 1. No policy
    context.matched_policy = nullptr;
    auto res1 = l1_variant_lookup_stage(context);
    EXPECT_EQ(res1.result, apg::StageResult::Continue);
    EXPECT_STREQ(res1.note, "no-policy");
    context.matched_policy = &policy;

    // 2. Cache disabled
    policy.cache.behavior = policy::CacheBehavior::Bypass;
    auto res2 = l1_variant_lookup_stage(context);
    EXPECT_EQ(res2.result, apg::StageResult::Continue);
    EXPECT_STREQ(res2.note, "cache-disabled");
    policy.cache.behavior = policy::CacheBehavior::Store;

    // 3. Variant policy disabled
    policy.cache.field_variant.enabled = false;
    auto res3 = l1_variant_lookup_stage(context);
    EXPECT_EQ(res3.result, apg::StageResult::Continue);
    EXPECT_STREQ(res3.note, "variant-policy-disabled");
    policy.cache.field_variant.enabled = true;

    // 4. Key not ready
    context.variant_cache_key_ready = false;
    auto res4 = l1_variant_lookup_stage(context);
    EXPECT_EQ(res4.result, apg::StageResult::Continue);
    EXPECT_STREQ(res4.note, "key-not-ready");
}

TEST_F(L1VariantStageTest, Store_EnforcesPreconditions) {
    // 1. Not admitted
    context.variant_admission_passed = false;
    auto res1 = l1_variant_store_stage(context);
    EXPECT_EQ(res1.result, apg::StageResult::Continue);
    EXPECT_STREQ(res1.note, "not-admitted");
    context.variant_admission_passed = true;

    // 2. Non-2xx
    context.response_status_code = 500;
    auto res2 = l1_variant_store_stage(context);
    EXPECT_EQ(res2.result, apg::StageResult::Continue);
    EXPECT_STREQ(res2.note, "non-2xx");
    context.response_status_code = 200;

    // 3. No body
    context.response_body = nullptr;
    context.response_body_len = 0;
    auto res3 = l1_variant_store_stage(context);
    EXPECT_EQ(res3.result, apg::StageResult::Continue);
    EXPECT_STREQ(res3.note, "no-body");
}

TEST_F(L1VariantStageTest, StoreAndLookup_Success_HonorsTtlCap) {
    // 1. Perform Store
    auto store_res = l1_variant_store_stage(context);
    EXPECT_EQ(store_res.result, apg::StageResult::Continue);
    EXPECT_STREQ(store_res.note, "stored");

    // 2. Run Lookup
    auto lookup_res = l1_variant_lookup_stage(context);
    EXPECT_EQ(lookup_res.result, apg::StageResult::SkipRemaining);
    EXPECT_STREQ(lookup_res.note, "l1-variant-hit");
    EXPECT_TRUE(context.cache_hit);
    EXPECT_STREQ(context.cache_layer, "L1_VAR");
    EXPECT_TRUE(context.should_return_immediate_response);
    EXPECT_EQ(context.cached_response.status_code, 200);
    EXPECT_STREQ(context.cached_response.content_type, "application/json");
    EXPECT_STREQ(context.cached_response.body, "{\"f1\":1,\"f2\":2}");

    // Verify expiry (capped at ttl_max_ms = 5000ms instead of cache.ttl_seconds = 10000ms)
    EXPECT_EQ(context.cached_response.expires_at_epoch_ms, context.request_epoch_ms + 5000);
}

} // namespace bytetaper::stages

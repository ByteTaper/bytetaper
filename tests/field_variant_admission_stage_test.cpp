// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "policy/route_policy.h"
#include "stages/field_variant_admission_stage.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::stages {

extern void field_variant_admission_test_reset();

class FieldVariantAdmissionStageTest : public ::testing::Test {
protected:
    apg::ApgTransformContext context{};
    policy::RoutePolicy policy{};

    void SetUp() override {
        field_variant_admission_test_reset();
        context.matched_policy = &policy;
        policy.route_id = "test-route";
        policy.cache.field_variant.enabled = true;
        policy.cache.field_variant.max_variants_per_route = 2;
        policy.cache.field_variant.min_field_count = 1;
        policy.cache.field_variant.max_field_count = 4;
        policy.cache.field_variant.admission_threshold = 2;

        context.variant_cache_key_ready = true;
        std::strncpy(context.variant_cache_key, "var:GET|r1|/path||version|f1,f2",
                     sizeof(context.variant_cache_key) - 1);
        context.selected_field_count = 2;
    }
};

TEST_F(FieldVariantAdmissionStageTest, Admission_ThrottledThenAdmitted) {
    // 1st request -> throttled (count = 1, threshold = 2)
    auto res1 = field_variant_admission_stage(context);
    EXPECT_EQ(res1.result, apg::StageResult::Continue);
    EXPECT_STREQ(res1.note, "throttled");
    EXPECT_FALSE(context.variant_admission_passed);

    // 2nd request -> admitted (count = 2, threshold = 2)
    auto res2 = field_variant_admission_stage(context);
    EXPECT_EQ(res2.result, apg::StageResult::Continue);
    EXPECT_STREQ(res2.note, "admitted");
    EXPECT_TRUE(context.variant_admission_passed);
}

TEST_F(FieldVariantAdmissionStageTest, Admission_EnforcesMinMaxFields) {
    // Below min fields
    context.selected_field_count = 0;
    auto res1 = field_variant_admission_stage(context);
    EXPECT_STREQ(res1.note, "below-min-fields");
    EXPECT_FALSE(context.variant_admission_passed);

    // Above max fields
    context.selected_field_count = 5;
    auto res2 = field_variant_admission_stage(context);
    EXPECT_STREQ(res2.note, "above-max-fields");
    EXPECT_FALSE(context.variant_admission_passed);
}

TEST_F(FieldVariantAdmissionStageTest, Admission_EnforcesMaxVariantsPerRoute) {
    // Admitted key 1
    std::strncpy(context.variant_cache_key, "var:key1", sizeof(context.variant_cache_key) - 1);
    field_variant_admission_stage(context); // count 1
    field_variant_admission_stage(context); // count 2 -> admitted
    EXPECT_TRUE(context.variant_admission_passed);

    // Admitted key 2
    std::strncpy(context.variant_cache_key, "var:key2", sizeof(context.variant_cache_key) - 1);
    field_variant_admission_stage(context); // count 1
    field_variant_admission_stage(context); // count 2 -> admitted
    EXPECT_TRUE(context.variant_admission_passed);

    // Key 3 should fail admission because max_variants_per_route is 2!
    std::strncpy(context.variant_cache_key, "var:key3", sizeof(context.variant_cache_key) - 1);
    field_variant_admission_stage(context);            // count 1
    auto res = field_variant_admission_stage(context); // count 2 -> should be throttled/rejected
    EXPECT_STREQ(res.note, "throttled");
    EXPECT_FALSE(context.variant_admission_passed);
}

TEST_F(FieldVariantAdmissionStageTest, Admission_BypassesIfDisabled) {
    policy.cache.field_variant.enabled = false;
    auto res = field_variant_admission_stage(context);
    EXPECT_STREQ(res.note, "variant-policy-disabled");
    EXPECT_FALSE(context.variant_admission_passed);
}

} // namespace bytetaper::stages

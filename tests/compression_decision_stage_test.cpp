// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "compression/compression_decision.h"
#include "policy/route_policy.h"
#include "stages/compression_decision_stage.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::stages {

class CompressionDecisionStageTest : public ::testing::Test {
protected:
    policy::RoutePolicy policy{};
    metrics::CompressionMetrics metrics{};

    void SetUp() override {
        policy.route_id = "test-route";
        policy.compression.enabled = true;
        policy.compression.min_size_bytes = 512;
        std::strncpy(policy.compression.eligible_content_types[0], "application/json",
                     policy::kMaxContentTypeLen - 1);
        policy.compression.eligible_content_type_count = 1;
    }

    CompressionDecisionContext make_eligible_context() {
        CompressionDecisionContext ctx{};
        ctx.matched_policy = &policy;
        ctx.client_accept_encoding.supports_gzip = true;
        ctx.client_accept_encoding.supports_br = true;
        ctx.response_status_code = 200;
        ctx.response_content_encoding.already_encoded = false;
        ctx.response_content_type = "application/json";
        ctx.response_content_type_len = std::strlen("application/json");
        ctx.response_body_len = 1024;
        ctx.response_body_size_known = true;
        ctx.compression_metrics = &metrics;
        return ctx;
    }
};

TEST_F(CompressionDecisionStageTest, FastPath_EligibleResponse_Candidate) {
    auto ctx = make_eligible_context();
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "compression-candidate");
    EXPECT_TRUE(ctx.compression_decision.evaluated);
    EXPECT_TRUE(ctx.compression_decision.candidate);
    EXPECT_EQ(ctx.compression_decision.skip_reason, compression::CompressionSkipReason::None);
    EXPECT_EQ(ctx.compression_decision.algorithm_hint, policy::CompressionAlgorithm::Brotli);
}

TEST_F(CompressionDecisionStageTest, FastPath_PolicyDisabled_NotCandidate) {
    policy.compression.enabled = false;
    auto ctx = make_eligible_context();
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "compression-disabled");
    EXPECT_FALSE(ctx.compression_decision.evaluated); // Short-circuits before evaluation
    EXPECT_FALSE(ctx.compression_decision.candidate);
}

TEST_F(CompressionDecisionStageTest, FastPath_NoClientSupport_NotCandidate) {
    auto ctx = make_eligible_context();
    ctx.client_accept_encoding = {}; // all false
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "no_client_support");
    EXPECT_TRUE(ctx.compression_decision.evaluated);
    EXPECT_FALSE(ctx.compression_decision.candidate);
    EXPECT_EQ(ctx.compression_decision.skip_reason,
              compression::CompressionSkipReason::NoClientSupport);
}

TEST_F(CompressionDecisionStageTest, FastPath_AlreadyEncoded_NotCandidate) {
    auto ctx = make_eligible_context();
    ctx.response_content_encoding.already_encoded = true;
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "already_encoded");
    EXPECT_TRUE(ctx.compression_decision.evaluated);
    EXPECT_FALSE(ctx.compression_decision.candidate);
    EXPECT_EQ(ctx.compression_decision.skip_reason,
              compression::CompressionSkipReason::AlreadyEncoded);
}

TEST_F(CompressionDecisionStageTest, FastPath_Non2xx_NotCandidate) {
    auto ctx = make_eligible_context();
    ctx.response_status_code = 404;
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "non_2xx_status");
    EXPECT_TRUE(ctx.compression_decision.evaluated);
    EXPECT_FALSE(ctx.compression_decision.candidate);
    EXPECT_EQ(ctx.compression_decision.skip_reason,
              compression::CompressionSkipReason::Non2xxStatus);
}

TEST_F(CompressionDecisionStageTest, FastPath_ContentTypeNotEligible_NotCandidate) {
    auto ctx = make_eligible_context();
    ctx.response_content_type = "image/png";
    ctx.response_content_type_len = std::strlen("image/png");
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "content_type_not_eligible");
    EXPECT_TRUE(ctx.compression_decision.evaluated);
    EXPECT_FALSE(ctx.compression_decision.candidate);
    EXPECT_EQ(ctx.compression_decision.skip_reason,
              compression::CompressionSkipReason::ContentTypeNotEligible);
}

TEST_F(CompressionDecisionStageTest, FastPath_TooSmall_NotCandidate) {
    auto ctx = make_eligible_context();
    ctx.response_body_len = 100; // below min_size_bytes=512
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "below_minimum");
    EXPECT_TRUE(ctx.compression_decision.evaluated);
    EXPECT_FALSE(ctx.compression_decision.candidate);
    EXPECT_EQ(ctx.compression_decision.skip_reason,
              compression::CompressionSkipReason::BelowMinimum);
}

TEST_F(CompressionDecisionStageTest, FastPath_SizeUnknown_NotCandidate) {
    auto ctx = make_eligible_context();
    ctx.response_body_size_known = false;
    auto result = evaluate_compression_decision_fast(ctx);

    EXPECT_EQ(result.result, apg::StageResult::Continue);
    EXPECT_STREQ(result.note, "size_unknown");
    EXPECT_TRUE(ctx.compression_decision.evaluated);
    EXPECT_FALSE(ctx.compression_decision.candidate);
    EXPECT_EQ(ctx.compression_decision.skip_reason,
              compression::CompressionSkipReason::SizeUnknown);
}

TEST_F(CompressionDecisionStageTest, Adapter_ParityTest_Eligible) {
    // 1. Fast path run
    auto fast_ctx = make_eligible_context();
    auto fast_result = evaluate_compression_decision_fast(fast_ctx);

    // 2. Adapter run
    apg::ApgTransformContext legacy_ctx{};
    legacy_ctx.matched_policy = &policy;
    legacy_ctx.client_accept_encoding.supports_gzip = true;
    legacy_ctx.client_accept_encoding.supports_br = true;
    legacy_ctx.response_status_code = 200;
    legacy_ctx.response_content_encoding.already_encoded = false;
    std::strncpy(legacy_ctx.response_content_type, "application/json",
                 sizeof(legacy_ctx.response_content_type) - 1);
    legacy_ctx.response_content_type_len = std::strlen("application/json");
    legacy_ctx.response_body_len = 1024;
    legacy_ctx.response_body_size_known = true;
    legacy_ctx.compression_metrics = &metrics;

    auto adapter_result = compression_decision_stage(legacy_ctx);

    EXPECT_EQ(adapter_result.result, fast_result.result);
    EXPECT_STREQ(adapter_result.note, fast_result.note);
    EXPECT_EQ(legacy_ctx.compression_decision.evaluated, fast_ctx.compression_decision.evaluated);
    EXPECT_EQ(legacy_ctx.compression_decision.candidate, fast_ctx.compression_decision.candidate);
    EXPECT_EQ(legacy_ctx.compression_decision.skip_reason,
              fast_ctx.compression_decision.skip_reason);
    EXPECT_EQ(legacy_ctx.compression_decision.algorithm_hint,
              fast_ctx.compression_decision.algorithm_hint);
}

TEST_F(CompressionDecisionStageTest, Adapter_ParityTest_Ineligible) {
    policy.compression.enabled = false;

    // 1. Fast path run
    auto fast_ctx = make_eligible_context();
    auto fast_result = evaluate_compression_decision_fast(fast_ctx);

    // 2. Adapter run
    apg::ApgTransformContext legacy_ctx{};
    legacy_ctx.matched_policy = &policy;
    legacy_ctx.compression_metrics = &metrics;

    auto adapter_result = compression_decision_stage(legacy_ctx);

    EXPECT_EQ(adapter_result.result, fast_result.result);
    EXPECT_STREQ(adapter_result.note, fast_result.note);
    EXPECT_EQ(legacy_ctx.compression_decision.evaluated, fast_ctx.compression_decision.evaluated);
    EXPECT_EQ(legacy_ctx.compression_decision.candidate, fast_ctx.compression_decision.candidate);
}

} // namespace bytetaper::stages

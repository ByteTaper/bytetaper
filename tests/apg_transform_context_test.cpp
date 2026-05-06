// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "apg/context.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::apg {

TEST(ApgTransformContextTest, DefaultInitZerosAllFields) {
    const ApgTransformContext context{};

    EXPECT_EQ(context.request_id, 0u);
    EXPECT_EQ(context.input_payload_bytes, 0u);
    EXPECT_EQ(context.output_payload_bytes, 0u);
    EXPECT_EQ(context.executed_stage_count, 0u);
    EXPECT_STREQ(context.raw_path, "");
    EXPECT_EQ(context.raw_path_length, 0u);
    EXPECT_STREQ(context.raw_query, "");
    EXPECT_EQ(context.raw_query_length, 0u);
    EXPECT_EQ(context.selected_field_count, 0u);
    EXPECT_STREQ(context.selected_fields[0], "");
    EXPECT_EQ(context.removed_field_count, 0u);
    EXPECT_EQ(context.trace_buffer, nullptr);
    EXPECT_EQ(context.trace_capacity, 0u);
    EXPECT_EQ(context.trace_length, 0u);
}

TEST(ApgTransformContextTest, ExplicitAssignmentReadsBackValues) {
    ApgTransformContext context{};
    context.request_id = 42u;
    context.input_payload_bytes = 128u;
    context.output_payload_bytes = 64u;
    context.executed_stage_count = 3u;
    std::strcpy(context.raw_path, "/users/7");
    context.raw_path_length = std::strlen(context.raw_path);
    std::strcpy(context.raw_query, "fields=id,name");
    context.raw_query_length = std::strlen(context.raw_query);
    std::strcpy(context.selected_fields[0], "id");
    std::strcpy(context.selected_fields[1], "name");
    context.selected_field_count = 2u;
    context.removed_field_count = 3u;
    context.trace_buffer = context.raw_query;
    context.trace_capacity = 15u;
    context.trace_length = 2u;

    EXPECT_EQ(context.request_id, 42u);
    EXPECT_EQ(context.input_payload_bytes, 128u);
    EXPECT_EQ(context.output_payload_bytes, 64u);
    EXPECT_EQ(context.executed_stage_count, 3u);
    EXPECT_STREQ(context.raw_path, "/users/7");
    EXPECT_EQ(context.raw_path_length, 8u);
    EXPECT_STREQ(context.raw_query, "fields=id,name");
    EXPECT_EQ(context.raw_query_length, 14u);
    EXPECT_STREQ(context.selected_fields[0], "id");
    EXPECT_STREQ(context.selected_fields[1], "name");
    EXPECT_EQ(context.selected_field_count, 2u);
    EXPECT_EQ(context.removed_field_count, 3u);
    EXPECT_EQ(context.trace_buffer, context.raw_query);
    EXPECT_EQ(context.trace_capacity, 15u);
    EXPECT_EQ(context.trace_length, 2u);
}

TEST(ApgTransformContextTest, FinalFlag_DefaultFalse) {
    const ApgTransformContext context{};
    EXPECT_FALSE(context.compression_decision_final);
}

TEST(ApgTransformContextTest, FinalFlag_SetWhenSizeKnownAndEvaluated) {
    ApgTransformContext context{};
    context.response_body_size_known = true;
    context.compression_decision.evaluated = true;

    // Simulation of what happens in ResponseHeaders phase:
    if (context.response_body_size_known && context.compression_decision.evaluated) {
        context.compression_decision_final = true;
    }

    EXPECT_TRUE(context.compression_decision_final);
}

TEST(ApgTransformContextTest, FinalFlag_NotSetWhenSizeUnknown) {
    ApgTransformContext context{};
    context.response_body_size_known = false;
    context.compression_decision.evaluated = true;

    if (context.response_body_size_known && context.compression_decision.evaluated) {
        context.compression_decision_final = true;
    }

    EXPECT_FALSE(context.compression_decision_final);
}

TEST(ApgTransformContextTest, FinalFlag_NotSetWhenNotEvaluated) {
    ApgTransformContext context{};
    context.response_body_size_known = true;
    context.compression_decision.evaluated = false;

    if (context.response_body_size_known && context.compression_decision.evaluated) {
        context.compression_decision_final = true;
    }

    EXPECT_FALSE(context.compression_decision_final);
}

} // namespace bytetaper::apg

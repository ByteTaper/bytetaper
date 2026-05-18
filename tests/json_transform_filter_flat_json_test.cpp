// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "json_transform/content_type.h"

#include <cstring>
#include <gtest/gtest.h>

namespace bytetaper::json_transform {

namespace {

void set_selected_fields(apg::ApgTransformContext* context, const char* first, const char* second,
                         const char* third = nullptr) {
    context->selected_field_count = 0;
    for (std::size_t index = 0; index < policy::kMaxFields; ++index) {
        context->selected_fields[index][0] = '\0';
    }

    const char* values[3] = { first, second, third };
    for (std::size_t index = 0; index < 3; ++index) {
        if (values[index] == nullptr) {
            continue;
        }
        std::strncpy(context->selected_fields[context->selected_field_count], values[index],
                     policy::kMaxFieldNameLen - 1);
        context->selected_fields[context->selected_field_count][policy::kMaxFieldNameLen - 1] =
            '\0';
        context->selected_field_count += 1;
    }
}

ParsedFlatJsonObject parse_or_fail(const char* body) {
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    EXPECT_EQ(status, FlatJsonParseStatus::Ok);
    return parsed;
}

} // namespace

TEST(JsonTransformFilterFlatJsonTest, FiltersExamplePayloadBySelectedFields) {
    const char* body = R"({"id":1,"name":"Andi","email":"a@example.com","address":"Jakarta"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", "name");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":1,"name":"Andi"})");
    EXPECT_EQ(output_length, std::strlen(output));
    EXPECT_EQ(context.removed_field_count, 2u);
}

TEST(JsonTransformFilterFlatJsonTest, SupportsAdditionalSelectedFieldCombination) {
    const char* body = R"({"id":1,"name":"Andi","email":"a@example.com","address":"Jakarta"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "address", "email");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"email":"a@example.com","address":"Jakarta"})");
    EXPECT_EQ(output_length, std::strlen(output));
}

TEST(JsonTransformFilterFlatJsonTest, PreservesOriginalSourceOrderInsteadOfSelectionOrder) {
    const char* body = R"({"id":1,"name":"Andi","email":"a@example.com"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "email", "id");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":1,"email":"a@example.com"})");
}

TEST(JsonTransformFilterFlatJsonTest, PreservesPrimitiveValueRepresentations) {
    const char* body = R"({"count":123,"flag":false,"empty":null,"name":"Andi","s":"123"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "count", "flag", "empty");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"count":123,"flag":false,"empty":null})");
}

TEST(JsonTransformFilterFlatJsonTest, PreservesNumberTokenShapes) {
    const char* body = R"({"i":123,"n":-42,"d":3.14,"e":6.02e23,"m":-1.5E-2})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "i", "n", "d");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"i":123,"n":-42,"d":3.14})");

    set_selected_fields(&context, "e", "m");
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"e":6.02e23,"m":-1.5E-2})");
}

TEST(JsonTransformFilterFlatJsonTest, KeepsStringAndNumberDistinct) {
    const char* body = R"({"as_string":"123","as_number":123})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "as_string", "as_number");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"as_string":"123","as_number":123})");
}

TEST(JsonTransformFilterFlatJsonTest, IgnoresUnknownSelectedFieldsSafely) {
    const char* body = R"({"id":1,"name":"Andi"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "unknown", "id");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":1})");
}

TEST(JsonTransformFilterFlatJsonTest, KeepsSourceOrderWithMixedKnownAndUnknownSelections) {
    const char* body = R"({"id":1,"name":"Andi","email":"a@example.com"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "email", "missing", "id");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":1,"email":"a@example.com"})");
}

TEST(JsonTransformFilterFlatJsonTest, ReturnsEmptyObjectForEmptyOrAllUnknownSelections) {
    const char* body = R"({"id":1,"name":"Andi"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext empty_context{};
    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, empty_context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, "{}");

    apg::ApgTransformContext unknown_context{};
    set_selected_fields(&unknown_context, "missing", nullptr);
    ASSERT_EQ(filter_flat_json_by_selected_fields(parsed, unknown_context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, "{}");
}

TEST(JsonTransformFilterFlatJsonTest, ReturnsOutputTooSmallAndKeepsWritesBounded) {
    const char* body = R"({"id":1,"name":"Andi"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", "name");

    char output[8] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::OutputTooSmall);
    EXPECT_EQ(output[sizeof(output) - 1], '\0');
    EXPECT_GT(output_length, sizeof(output) - 1);
}

TEST(JsonTransformFilterFlatJsonTest, ReturnsInvalidInputForInvalidPointers) {
    const char* body = R"({"id":1})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);
    apg::ApgTransformContext context{};

    std::size_t output_length = 0;
    EXPECT_EQ(filter_flat_json_by_selected_fields(parsed, context, nullptr, 16, &output_length),
              FlatJsonFilterStatus::InvalidInput);

    char output[16] = {};
    EXPECT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output), nullptr),
              FlatJsonFilterStatus::InvalidInput);
}

TEST(JsonTransformFilterFlatJsonTest, ReturnsSkipUnsupportedForUnusableParsedInput) {
    ParsedFlatJsonObject parsed{};
    apg::ApgTransformContext context{};
    char output[16] = {};
    std::size_t output_length = 0;

    EXPECT_EQ(filter_flat_json_by_selected_fields(parsed, context, output, sizeof(output),
                                                  &output_length),
              FlatJsonFilterStatus::SkipUnsupported);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleReturnsOriginalBodyWhenFilteringDisabled) {
    const char* body = R"({"id":1,"name":"Andi","email":"a@example.com"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", nullptr);

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, &parsed, context, false, output,
                                                     sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, body);
    EXPECT_EQ(output_length, std::strlen(body));
    EXPECT_EQ(context.removed_field_count, 0u);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleReturnsOriginalBodyEvenIfSelectionWouldFilter) {
    const char* body = R"({"id":1,"name":"Andi","email":"a@example.com","address":"Jakarta"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", "name");
    context.removed_field_count = 7u;

    char output[256] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, &parsed, context, false, output,
                                                     sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, body);
    EXPECT_EQ(context.removed_field_count, 0u);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledPathUsesExistingFilteringBehavior) {
    const char* body = R"({"id":1,"name":"Andi","email":"a@example.com"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "email", "id");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, &parsed, context, true, output,
                                                     sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":1,"email":"a@example.com"})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledSupportsSimpleNestedSelection) {
    const char* body = R"({"user":{"name":"A","age":1},"id":9})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "user.name", nullptr);

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"user":{"name":"A"}})");
    EXPECT_EQ(context.removed_field_count, 2u);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledSupportsMultiLevelNestedSelection) {
    const char* body =
        R"({"user":{"name":"A","profile":{"email":"a@example.com","phone":"x"},"age":1},"id":9})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "user.profile.email", "user.name");

    char output[256] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"user":{"name":"A","profile":{"email":"a@example.com"}}})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledSupportsMixedFlatAndNestedSelection) {
    const char* body = R"({"id":9,"user":{"name":"A","age":1},"tenant":"t1"})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "user.name", "id");

    char output[256] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":9,"user":{"name":"A"}})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledIgnoresUnknownNestedPathsSafely) {
    const char* body = R"({"id":9,"user":{"name":"A"}})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "user.missing", "id");

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":9})");
    EXPECT_EQ(context.removed_field_count, 2u);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledSupportsArrayObjectWildcardFiltering) {
    const char* body = R"({"items":[{"name":"A","age":1},{"name":"B","age":2}]})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "items[].name", nullptr);

    char output[256] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"items":[{"name":"A"},{"name":"B"}]})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledSupportsMixedRootAndArraySelection) {
    const char* body = R"({"id":7,"items":[{"name":"A","age":1},{"name":"B","age":2}]})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "items[].name", "id");

    char output[256] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":7,"items":[{"name":"A"},{"name":"B"}]})");
    EXPECT_EQ(context.removed_field_count, 2u);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledSupportsDeepArrayObjectPath) {
    const char* body =
        R"({"users":[{"profile":{"email":"a@example.com","name":"A"}},{"profile":{"email":"b@example.com","name":"B"}}]})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "users[].profile.email", nullptr);

    char output[512] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(
        output,
        R"({"users":[{"profile":{"email":"a@example.com"}},{"profile":{"email":"b@example.com"}}]})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledIgnoresUnknownArraySubpathsSafely) {
    const char* body = R"({"items":[{"name":"A"},{"name":"B"}]})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "items[].missing", nullptr);

    char output[256] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledPreservesPrimitiveTokensInsideArrayObjects) {
    const char* body =
        R"({"items":[{"n":123,"b":false,"z":null,"s":"123"},{"n":-1.5E-2,"b":true,"z":null,"s":"x"}]})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "items[].n", "items[].s");

    char output[512] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"items":[{"n":123,"s":"123"},{"n":-1.5E-2,"s":"x"}]})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledSkipsUnselectedArrayValuesSafely) {
    const char* body = R"({"id":9,"items":[{"name":"A"}]})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", nullptr);

    char output[128] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":9})");
}

TEST(JsonTransformFilterFlatJsonTest, ToggleEnabledReturnsSkipUnsupportedForArrayPathNotation) {
    const char* body = R"({"items":{"name":"A"}})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    set_selected_fields(&context, "items[0].name", nullptr);

    char output[128] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::SkipUnsupported);
    EXPECT_EQ(context.removed_field_count, 0u);
}

TEST(JsonTransformFilterFlatJsonTest, UnknownSelectionsDoNotInflateRemovedFieldCount) {
    const char* body = R"({"id":9,"user":{"name":"A"}})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext with_unknown{};
    set_selected_fields(&with_unknown, "user.missing", "id");
    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, with_unknown,
                                                     true, output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);

    apg::ApgTransformContext baseline{};
    set_selected_fields(&baseline, "id", nullptr);
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, baseline, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);

    EXPECT_EQ(with_unknown.removed_field_count, baseline.removed_field_count);
}

TEST(JsonTransformFilterFlatJsonTest, InvalidJsonSafeErrorResetsRemovedFieldCount) {
    apg::ApgTransformContext context{};
    context.removed_field_count = 42u;

    char output[128] = {};
    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle("{invalid", FlatJsonParseStatus::InvalidJson,
                                                     nullptr, context, true, output, sizeof(output),
                                                     &output_length),
              FlatJsonFilterStatus::InvalidJsonSafeError);
    EXPECT_STREQ(output, R"({"error":"invalid_json"})");
    EXPECT_EQ(context.removed_field_count, 0u);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleDisabledReturnsOutputTooSmallWhenBodyDoesNotFit) {
    const char* body = R"({"id":1,"name":"Andi"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    char output[8] = {};
    std::size_t output_length = 0;

    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, &parsed, context, false, output,
                                                     sizeof(output), &output_length),
              FlatJsonFilterStatus::OutputTooSmall);
    EXPECT_EQ(output[sizeof(output) - 1], '\0');
    EXPECT_GT(output_length, sizeof(output) - 1);
}

TEST(JsonTransformFilterFlatJsonTest, ToggleReturnsInvalidInputForInvalidPointers) {
    const char* body = R"({"id":1})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);
    apg::ApgTransformContext context{};
    char output[16] = {};

    std::size_t output_length = 0;
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, &parsed, context, false, nullptr,
                                                     sizeof(output), &output_length),
              FlatJsonFilterStatus::InvalidInput);
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, &parsed, context, false, output,
                                                     sizeof(output), nullptr),
              FlatJsonFilterStatus::InvalidInput);
    EXPECT_EQ(transform_flat_json_with_filter_toggle(nullptr, &parsed, context, false, output,
                                                     sizeof(output), &output_length),
              FlatJsonFilterStatus::InvalidInput);
    EXPECT_EQ(transform_flat_json_with_filter_toggle(body, nullptr, context, true, output,
                                                     sizeof(output), &output_length),
              FlatJsonFilterStatus::InvalidInput);
}

TEST(JsonTransformFilterFlatJsonTest, FlatDenylistFiltersBody) {
    const char* body = R"({"id":1,"secret_token":"abc","name":"Andi"})";
    const ParsedFlatJsonObject parsed = parse_or_fail(body);

    apg::ApgTransformContext context{};
    policy::FieldFilterPolicy filter_policy{};
    filter_policy.mode = policy::FieldFilterMode::Denylist;
    std::strncpy(filter_policy.fields[0], "secret_token", policy::kMaxFieldNameLen - 1);
    filter_policy.field_count = 1;

    char output[128] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(filter_flat_json_by_policy_denylist(parsed, context, filter_policy, output,
                                                  sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":1,"name":"Andi"})");
    EXPECT_EQ(context.removed_field_count, 1u);
}

TEST(JsonTransformFilterFlatJsonTest, NestedDenylistFiltersBody) {
    const char* body =
        R"({"id":1,"user":{"name":"Andi","secret_token":"xyz"},"debug_internal":"log"})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);
    ASSERT_EQ(parse_status, FlatJsonParseStatus::SkipUnsupported);

    apg::ApgTransformContext context{};
    policy::RoutePolicy matched_policy{};
    matched_policy.field_filter.mode = policy::FieldFilterMode::Denylist;
    std::strncpy(matched_policy.field_filter.fields[0], "user.secret_token",
                 policy::kMaxFieldNameLen - 1);
    std::strncpy(matched_policy.field_filter.fields[1], "debug_internal",
                 policy::kMaxFieldNameLen - 1);
    matched_policy.field_filter.field_count = 2;
    context.matched_policy = &matched_policy;

    char output[256] = {};
    std::size_t output_length = 0;
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, R"({"id":1,"user":{"name":"Andi"}})");
    EXPECT_EQ(context.removed_field_count, 2u);
}

TEST(JsonTransformFilterFlatJsonTest, DenylistClientQueryBypassLeakage) {
    const char* body = R"({"id":1,"secret_token":"abc","name":"Andi"})";
    ParsedFlatJsonObject parsed{};
    const FlatJsonParseStatus parse_status =
        parse_flat_json_object(body, JsonResponseKind::EligibleJson, &parsed);

    apg::ApgTransformContext context{};
    policy::RoutePolicy matched_policy{};
    matched_policy.field_filter.mode = policy::FieldFilterMode::Denylist;
    std::strncpy(matched_policy.field_filter.fields[0], "secret_token",
                 policy::kMaxFieldNameLen - 1);
    matched_policy.field_filter.field_count = 1;
    context.matched_policy = &matched_policy;

    // Simulate client requested ONLY secret_token (?fields=secret_token)
    // Which got filtered out by denylist policy, resulting in 0 selected fields,
    // but client_query_present = true.
    context.client_query_present = true;
    context.selected_field_count = 0;

    char output[128] = {};
    std::size_t output_length = 0;

    // We expect it to use filter_nested_json_by_selected_fields and output empty JSON `{}`
    // instead of falling back to policy denylist (which would leak id and name)!
    ASSERT_EQ(transform_flat_json_with_filter_toggle(body, parse_status, &parsed, context, true,
                                                     output, sizeof(output), &output_length),
              FlatJsonFilterStatus::Ok);
    EXPECT_STREQ(output, "{}");
    EXPECT_EQ(context.removed_field_count, 3u); // all 3 fields filtered
}

} // namespace bytetaper::json_transform

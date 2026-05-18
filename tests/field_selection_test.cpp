// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "field_selection/request_target.h"

#include <cstring>
#include <gtest/gtest.h>
#include <string>

namespace bytetaper::field_selection {

namespace {

struct QueryScenario {
    const char* raw_query;
    const char* expected_fields[policy::kMaxFields];
    std::size_t expected_count;
};

void set_raw_query(apg::ApgTransformContext* context, const char* raw_query) {
    if (context == nullptr) {
        return;
    }
    if (raw_query == nullptr) {
        context->raw_query[0] = '\0';
        context->raw_query_length = 0;
        return;
    }

    std::strncpy(context->raw_query, raw_query, apg::ApgTransformContext::kRawQueryBufferSize - 1);
    context->raw_query[apg::ApgTransformContext::kRawQueryBufferSize - 1] = '\0';
    context->raw_query_length = std::strlen(context->raw_query);
}

void expect_parsed_fields(const ParsedFieldsQuery& parsed_fields, const QueryScenario& scenario) {
    EXPECT_EQ(parsed_fields.field_count, scenario.expected_count);
    for (std::size_t i = 0; i < scenario.expected_count; ++i) {
        EXPECT_STREQ(parsed_fields.fields[i], scenario.expected_fields[i]);
    }
}

void expect_context_selected_fields(const apg::ApgTransformContext& context,
                                    const QueryScenario& scenario) {
    EXPECT_EQ(context.selected_field_count, scenario.expected_count);
    for (std::size_t i = 0; i < scenario.expected_count; ++i) {
        EXPECT_STREQ(context.selected_fields[i], scenario.expected_fields[i]);
    }
}

void set_selected_fields(apg::ApgTransformContext* context, const char* first, const char* second,
                         const char* third = nullptr) {
    context->selected_field_count = 0;
    for (std::size_t i = 0; i < policy::kMaxFields; ++i) {
        context->selected_fields[i][0] = '\0';
    }

    const char* values[3] = { first, second, third };
    for (std::size_t i = 0; i < 3; ++i) {
        if (values[i] == nullptr) {
            continue;
        }
        std::strncpy(context->selected_fields[context->selected_field_count], values[i],
                     policy::kMaxFieldNameLen - 1);
        context->selected_fields[context->selected_field_count][policy::kMaxFieldNameLen - 1] =
            '\0';
        context->selected_field_count += 1;
    }
}

const QueryScenario kCanonicalQueryScenarios[] = {
    { "fields=id,name,email", { "id", "name", "email" }, 3 },
    { "page=1&fields=id,name&sort=desc", { "id", "name" }, 2 },
    { "sort=desc&page=1", { nullptr }, 0 },
    { "fields=", { nullptr }, 0 },
    { "fields=,,", { nullptr }, 0 },
    { "a=1&fields=&b=2", { nullptr }, 0 },
    { "fields=id&fields=ignored", { "id" }, 1 },
    { "fields=id,,name,", { "id", "name" }, 2 },
};

} // namespace

TEST(FieldSelectionRequestTargetTest, ExtractsPathAndQueryForCommonCases) {
    struct RequestTargetCase {
        const char* request_target;
        const char* expected_path;
        const char* expected_query;
    };

    const RequestTargetCase cases[] = {
        { "/users/1?fields=id,name,email", "/users/1", "fields=id,name,email" },
        { "/health", "/health", "" },
        { "?fields=id", "", "fields=id" },
        { "", "", "" },
    };

    for (const RequestTargetCase& test_case : cases) {
        apg::ApgTransformContext context{};
        EXPECT_TRUE(extract_raw_path_and_query(test_case.request_target, &context));
        EXPECT_STREQ(context.raw_path, test_case.expected_path);
        EXPECT_STREQ(context.raw_query, test_case.expected_query);
        EXPECT_EQ(context.raw_path_length, std::strlen(test_case.expected_path));
        EXPECT_EQ(context.raw_query_length, std::strlen(test_case.expected_query));
    }
}

TEST(FieldSelectionRequestTargetTest, HandlesNullInputAsEmpty) {
    apg::ApgTransformContext context{};
    context.raw_path[0] = 'x';
    context.raw_query[0] = 'y';

    EXPECT_TRUE(extract_raw_path_and_query(nullptr, &context));
    EXPECT_STREQ(context.raw_path, "");
    EXPECT_STREQ(context.raw_query, "");
    EXPECT_EQ(context.raw_path_length, 0u);
    EXPECT_EQ(context.raw_query_length, 0u);
}

TEST(FieldSelectionRequestTargetTest, ReturnsFalseForNullContext) {
    EXPECT_FALSE(extract_raw_path_and_query("/users", nullptr));
}

TEST(FieldSelectionRequestTargetTest, TruncatesPathAndQuerySafely) {
    const std::string long_path(apg::ApgTransformContext::kRawPathBufferSize + 32, 'p');
    const std::string long_query(apg::ApgTransformContext::kRawQueryBufferSize + 32, 'q');
    const std::string request_target = long_path + "?" + long_query;

    apg::ApgTransformContext context{};
    EXPECT_TRUE(extract_raw_path_and_query(request_target.c_str(), &context));

    EXPECT_EQ(context.raw_path_length, apg::ApgTransformContext::kRawPathBufferSize - 1);
    EXPECT_EQ(context.raw_query_length, apg::ApgTransformContext::kRawQueryBufferSize - 1);
    EXPECT_EQ(context.raw_path[context.raw_path_length], '\0');
    EXPECT_EQ(context.raw_query[context.raw_query_length], '\0');
}

TEST(FieldSelectionParseTest, ParsesCanonicalScenarios) {
    for (const QueryScenario& scenario : kCanonicalQueryScenarios) {
        apg::ApgTransformContext context{};
        set_raw_query(&context, scenario.raw_query);
        ParsedFieldsQuery parsed_fields{};
        EXPECT_TRUE(parse_fields_query_parameter(context, &parsed_fields));
        expect_parsed_fields(parsed_fields, scenario);
    }
}

TEST(FieldSelectionParseTest, HandlesBoundarySegmentsAndFirstFieldsPrecedence) {
    const QueryScenario cases[] = {
        { "fields&x=1", { nullptr }, 0 },     { "fields", { nullptr }, 0 },
        { "fields=id&", { "id" }, 1 },        { "fields=&fields=id", { nullptr }, 0 },
        { "a=1&fields&b=2", { nullptr }, 0 }, { "a=1&fields=id&fields=", { "id" }, 1 },
    };

    for (const QueryScenario& scenario : cases) {
        apg::ApgTransformContext context{};
        set_raw_query(&context, scenario.raw_query);
        ParsedFieldsQuery parsed_fields{};
        EXPECT_TRUE(parse_fields_query_parameter(context, &parsed_fields));
        expect_parsed_fields(parsed_fields, scenario);
    }
}

TEST(FieldSelectionParseTest, EnforcesCountAndNameBoundsSafely) {
    std::string query = "fields=";
    for (std::size_t i = 0; i < policy::kMaxFields + 4; ++i) {
        if (i > 0) {
            query += ",";
        }
        query += "f";
        query += std::to_string(i);
    }
    query += ",";
    query += std::string(policy::kMaxFieldNameLen + 16, 'x');

    apg::ApgTransformContext context{};
    set_raw_query(&context, query.c_str());

    ParsedFieldsQuery parsed_fields{};
    EXPECT_TRUE(parse_fields_query_parameter(context, &parsed_fields));
    EXPECT_EQ(parsed_fields.field_count, policy::kMaxFields);
    for (std::size_t i = 0; i < parsed_fields.field_count; ++i) {
        EXPECT_LT(std::strlen(parsed_fields.fields[i]), policy::kMaxFieldNameLen);
    }
}

TEST(FieldSelectionParseTest, ReturnsFalseForNullOutput) {
    apg::ApgTransformContext context{};
    set_raw_query(&context, "fields=id");
    EXPECT_FALSE(parse_fields_query_parameter(context, nullptr));
}

TEST(FieldSelectionStoreTest, StoresCanonicalScenariosIntoContext) {
    for (const QueryScenario& scenario : kCanonicalQueryScenarios) {
        apg::ApgTransformContext context{};
        set_raw_query(&context, scenario.raw_query);
        EXPECT_TRUE(parse_and_store_selected_fields(&context));
        expect_context_selected_fields(context, scenario);
    }
}

TEST(FieldSelectionStoreTest, OverwritesPreviousSelectionsDeterministically) {
    apg::ApgTransformContext context{};
    set_raw_query(&context, "fields=id,name");
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    ASSERT_EQ(context.selected_field_count, 2u);

    set_raw_query(&context, "fields=");
    EXPECT_TRUE(parse_and_store_selected_fields(&context));
    EXPECT_EQ(context.selected_field_count, 0u);
    EXPECT_STREQ(context.selected_fields[0], "");
    EXPECT_STREQ(context.selected_fields[1], "");
}

TEST(FieldSelectionStoreTest, ReturnsFalseForNullContext) {
    EXPECT_FALSE(parse_and_store_selected_fields(nullptr));
}

TEST(FieldSelectionCountContractTest, ParseStoreAndPolicyUpdateCanonicalCount) {
    apg::ApgTransformContext context{};
    set_raw_query(&context, "fields=id,internal,name");
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    EXPECT_EQ(context.selected_field_count, 3u);

    policy::FieldFilterPolicy filter{};
    filter.mode = policy::FieldFilterMode::Denylist;
    std::strcpy(filter.fields[0], "internal");
    filter.field_count = 1;

    ASSERT_TRUE(enforce_selected_fields_policy(&context, filter));
    EXPECT_EQ(context.selected_field_count, 2u);
    EXPECT_STREQ(context.selected_fields[0], "id");
    EXPECT_STREQ(context.selected_fields[1], "name");
}

TEST(FieldSelectionCountContractTest, AllDisallowedAfterPolicyResultsInZeroCount) {
    apg::ApgTransformContext context{};
    set_raw_query(&context, "fields=internal,secret");
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    ASSERT_EQ(context.selected_field_count, 2u);

    policy::FieldFilterPolicy filter{};
    filter.mode = policy::FieldFilterMode::Denylist;
    std::strcpy(filter.fields[0], "internal");
    std::strcpy(filter.fields[1], "secret");
    filter.field_count = 2;

    ASSERT_TRUE(enforce_selected_fields_policy(&context, filter));
    EXPECT_EQ(context.selected_field_count, 0u);
    EXPECT_STREQ(context.selected_fields[0], "");
    EXPECT_STREQ(context.selected_fields[1], "");
}

TEST(FieldSelectionCountContractTest, EmptyOrMissingFieldsKeepCountAtZero) {
    const char* queries[] = { "fields=", "sort=desc&page=1", "fields=,," };
    for (const char* query : queries) {
        apg::ApgTransformContext context{};
        set_raw_query(&context, query);
        ASSERT_TRUE(parse_and_store_selected_fields(&context));
        EXPECT_EQ(context.selected_field_count, 0u);

        policy::FieldFilterPolicy filter{};
        filter.mode = policy::FieldFilterMode::Allowlist;
        std::strcpy(filter.fields[0], "id");
        filter.field_count = 1;
        ASSERT_TRUE(enforce_selected_fields_policy(&context, filter));
        EXPECT_EQ(context.selected_field_count, 0u);
    }
}

TEST(FieldSelectionCountContractTest, NullInputSafetyForHelpers) {
    ParsedFieldsQuery parsed_fields{};
    apg::ApgTransformContext context{};
    set_raw_query(&context, "fields=id");
    ASSERT_TRUE(parse_fields_query_parameter(context, &parsed_fields));
    ASSERT_EQ(parsed_fields.field_count, 1u);

    EXPECT_FALSE(parse_fields_query_parameter(context, nullptr));
    EXPECT_FALSE(parse_and_store_selected_fields(nullptr));
    policy::FieldFilterPolicy filter{};
    EXPECT_FALSE(enforce_selected_fields_policy(nullptr, filter));

    // Existing valid context state remains defined and usable after null-input checks.
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    EXPECT_EQ(context.selected_field_count, 1u);
}

TEST(FieldSelectionPolicyTest, AllowlistCompactsAndPreservesOrder) {
    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", "internal", "name");

    policy::FieldFilterPolicy filter{};
    filter.mode = policy::FieldFilterMode::Allowlist;
    std::strcpy(filter.fields[0], "id");
    std::strcpy(filter.fields[1], "name");
    filter.field_count = 2;

    EXPECT_TRUE(enforce_selected_fields_policy(&context, filter));
    EXPECT_EQ(context.selected_field_count, 2u);
    EXPECT_STREQ(context.selected_fields[0], "id");
    EXPECT_STREQ(context.selected_fields[1], "name");
    EXPECT_STREQ(context.selected_fields[2], "");
}

TEST(FieldSelectionPolicyTest, AllowlistWithNoAllowedFieldsBecomesEmpty) {
    apg::ApgTransformContext context{};
    set_selected_fields(&context, "status", "internal");

    policy::FieldFilterPolicy filter{};
    filter.mode = policy::FieldFilterMode::Allowlist;
    std::strcpy(filter.fields[0], "id");
    filter.field_count = 1;

    EXPECT_TRUE(enforce_selected_fields_policy(&context, filter));
    EXPECT_EQ(context.selected_field_count, 0u);
    EXPECT_STREQ(context.selected_fields[0], "");
    EXPECT_STREQ(context.selected_fields[1], "");
}

TEST(FieldSelectionPolicyTest, DenylistRemovesDeniedAndKeepsOthers) {
    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", "internal", "name");

    policy::FieldFilterPolicy filter{};
    filter.mode = policy::FieldFilterMode::Denylist;
    std::strcpy(filter.fields[0], "internal");
    filter.field_count = 1;

    EXPECT_TRUE(enforce_selected_fields_policy(&context, filter));
    EXPECT_EQ(context.selected_field_count, 2u);
    EXPECT_STREQ(context.selected_fields[0], "id");
    EXPECT_STREQ(context.selected_fields[1], "name");
}

TEST(FieldSelectionPolicyTest, NoneModeKeepsSelectionsUnchanged) {
    policy::FieldFilterPolicy filter{};
    filter.mode = policy::FieldFilterMode::None;

    apg::ApgTransformContext non_empty{};
    set_selected_fields(&non_empty, "id", "name");
    EXPECT_TRUE(enforce_selected_fields_policy(&non_empty, filter));
    EXPECT_EQ(non_empty.selected_field_count, 2u);
    EXPECT_STREQ(non_empty.selected_fields[0], "id");
    EXPECT_STREQ(non_empty.selected_fields[1], "name");

    apg::ApgTransformContext empty{};
    EXPECT_TRUE(enforce_selected_fields_policy(&empty, filter));
    EXPECT_EQ(empty.selected_field_count, 0u);
}

TEST(FieldSelectionPolicyTest, EmptyFieldsInputThenPolicyEnforcementStaysSafe) {
    apg::ApgTransformContext context{};
    set_raw_query(&context, "fields=");
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    ASSERT_EQ(context.selected_field_count, 0u);

    policy::FieldFilterPolicy filter{};
    filter.mode = policy::FieldFilterMode::Allowlist;
    std::strcpy(filter.fields[0], "id");
    filter.field_count = 1;

    EXPECT_TRUE(enforce_selected_fields_policy(&context, filter));
    EXPECT_EQ(context.selected_field_count, 0u);
}

TEST(FieldSelectionPolicyTest, ReturnsFalseForNullContext) {
    policy::FieldFilterPolicy filter{};
    EXPECT_FALSE(enforce_selected_fields_policy(nullptr, filter));
}

TEST(CompiledFieldSelectionTest, CompileExactPath) {
    apg::ApgTransformContext context{};
    std::strcpy(context.selected_fields[0], "id");
    context.selected_field_count = 1;

    CompiledFieldSelection sel{};
    compile_field_selection(context, &sel);

    EXPECT_EQ(sel.exact_count, 1u);
    EXPECT_STREQ(sel.exact_paths[0], "id");
    EXPECT_EQ(sel.exact_lens[0], 2u);
    EXPECT_EQ(sel.exact_hashes[0], field_hash("id"));

    EXPECT_EQ(sel.descendant_count, 1u);
    EXPECT_STREQ(sel.descendant_paths[0], "id");
    EXPECT_EQ(sel.descendant_lens[0], 2u);
    EXPECT_EQ(sel.descendant_hashes[0], field_hash("id"));
}

TEST(CompiledFieldSelectionTest, CompileMultipleFields) {
    apg::ApgTransformContext context{};
    std::strcpy(context.selected_fields[0], "id");
    std::strcpy(context.selected_fields[1], "meta.created");
    context.selected_field_count = 2;

    CompiledFieldSelection sel{};
    compile_field_selection(context, &sel);

    EXPECT_EQ(sel.exact_count, 2u);
    EXPECT_STREQ(sel.exact_paths[1], "meta.created");
    EXPECT_EQ(sel.exact_lens[1], 12u);
    EXPECT_EQ(sel.exact_hashes[1], field_hash("meta.created"));
}

TEST(CompiledFieldSelectionTest, MatchExactPath) {
    apg::ApgTransformContext context{};
    std::strcpy(context.selected_fields[0], "id");
    std::strcpy(context.selected_fields[1], "name");
    context.selected_field_count = 2;

    CompiledFieldSelection sel{};
    compile_field_selection(context, &sel);

    SelectionMatchResult res = match_selection_compiled(sel, "id");
    EXPECT_TRUE(res.exact_selected);
    EXPECT_FALSE(res.has_selected_descendant);

    SelectionMatchResult res2 = match_selection_compiled(sel, "name");
    EXPECT_TRUE(res2.exact_selected);
    EXPECT_FALSE(res2.has_selected_descendant);
}

TEST(CompiledFieldSelectionTest, MatchDescendantPath) {
    apg::ApgTransformContext context{};
    std::strcpy(context.selected_fields[0], "meta.created");
    std::strcpy(context.selected_fields[1], "items[]");
    context.selected_field_count = 2;

    CompiledFieldSelection sel{};
    compile_field_selection(context, &sel);

    SelectionMatchResult res = match_selection_compiled(sel, "meta");
    EXPECT_FALSE(res.exact_selected);
    EXPECT_TRUE(res.has_selected_descendant);

    SelectionMatchResult res2 = match_selection_compiled(sel, "items");
    EXPECT_FALSE(res2.exact_selected);
    EXPECT_TRUE(res2.has_selected_descendant);
}

TEST(CompiledFieldSelectionTest, NoMatchForUnrelatedPath) {
    apg::ApgTransformContext context{};
    std::strcpy(context.selected_fields[0], "id");
    context.selected_field_count = 1;

    CompiledFieldSelection sel{};
    compile_field_selection(context, &sel);

    SelectionMatchResult res = match_selection_compiled(sel, "name");
    EXPECT_FALSE(res.exact_selected);
    EXPECT_FALSE(res.has_selected_descendant);

    SelectionMatchResult res2 = match_selection_compiled(sel, "identity");
    EXPECT_FALSE(res2.exact_selected);
    EXPECT_FALSE(res2.has_selected_descendant);
}

TEST(CompiledFieldSelectionTest, NullPathSafe) {
    CompiledFieldSelection sel{};
    SelectionMatchResult res = match_selection_compiled(sel, nullptr);
    EXPECT_FALSE(res.exact_selected);
    EXPECT_FALSE(res.has_selected_descendant);
}

TEST(FieldSelectionRequestTargetTest, AllowlistDisallowedQueryFieldDropped) {
    apg::ApgTransformContext context{};
    set_selected_fields(&context, "id", "secret_token", "name");

    policy::FieldFilterPolicy policy_filter{};
    policy_filter.mode = policy::FieldFilterMode::Allowlist;
    std::strncpy(policy_filter.fields[0], "id", policy::kMaxFieldNameLen - 1);
    std::strncpy(policy_filter.fields[1], "name", policy::kMaxFieldNameLen - 1);
    policy_filter.field_count = 2;

    EXPECT_TRUE(enforce_selected_fields_policy(&context, policy_filter));
    EXPECT_EQ(context.selected_field_count, 2u);
    EXPECT_STREQ(context.selected_fields[0], "id");
    EXPECT_STREQ(context.selected_fields[1], "name");
}

TEST(FieldSelectionRequestTargetTest, ClientQueryPresentFlagAndDisallowedIntersection) {
    apg::ApgTransformContext context{};

    // Case A: No fields query parameter
    set_raw_query(&context, "other_param=1");
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    EXPECT_FALSE(context.client_query_present);
    EXPECT_EQ(context.selected_field_count, 0u);

    // Case B: Fields query parameter is present but empty
    set_raw_query(&context, "fields=");
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    EXPECT_TRUE(context.client_query_present);
    EXPECT_EQ(context.selected_field_count, 0u);

    // Case C: Fields query parameter is present and non-empty
    set_raw_query(&context, "fields=id,secret_token");
    ASSERT_TRUE(parse_and_store_selected_fields(&context));
    EXPECT_TRUE(context.client_query_present);
    EXPECT_EQ(context.selected_field_count, 2u);

    // Case D: Intersect with a Denylist policy denying "secret_token"
    policy::FieldFilterPolicy policy_filter{};
    policy_filter.mode = policy::FieldFilterMode::Denylist;
    std::strncpy(policy_filter.fields[0], "secret_token", policy::kMaxFieldNameLen - 1);
    policy_filter.field_count = 1;

    EXPECT_TRUE(enforce_selected_fields_policy(&context, policy_filter));
    // secret_token is denied, so it gets dropped. id remains.
    EXPECT_EQ(context.selected_field_count, 1u);
    EXPECT_STREQ(context.selected_fields[0], "id");
    // Client query present must still remain true!
    EXPECT_TRUE(context.client_query_present);

    // Case E: Fully denied query selection (Finding 1 case)
    apg::ApgTransformContext context2{};
    set_raw_query(&context2, "fields=secret_token");
    ASSERT_TRUE(parse_and_store_selected_fields(&context2));
    EXPECT_TRUE(context2.client_query_present);
    EXPECT_EQ(context2.selected_field_count, 1u);

    EXPECT_TRUE(enforce_selected_fields_policy(&context2, policy_filter));
    // secret_token gets dropped, making count 0
    EXPECT_EQ(context2.selected_field_count, 0u);
    // client_query_present is STILL true!
    EXPECT_TRUE(context2.client_query_present);
}

} // namespace bytetaper::field_selection

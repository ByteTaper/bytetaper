// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "field_selection/request_target.h"

#include <cstddef>

namespace bytetaper::field_selection {

namespace {

std::size_t bounded_copy(const char* source, std::size_t source_length, char* destination,
                         std::size_t destination_capacity) {
    if (destination == nullptr || destination_capacity == 0) {
        return 0;
    }

    std::size_t copied_length = source_length;
    if (copied_length + 1 > destination_capacity) {
        copied_length = destination_capacity - 1;
    }

    for (std::size_t i = 0; i < copied_length; ++i) {
        destination[i] = source[i];
    }
    destination[copied_length] = '\0';
    return copied_length;
}

void clear_path_and_query(apg::ApgTransformContext* context) {
    context->raw_path_length =
        bounded_copy("", 0, context->raw_path, apg::ApgTransformContext::kRawPathBufferSize);
    context->raw_query_length =
        bounded_copy("", 0, context->raw_query, apg::ApgTransformContext::kRawQueryBufferSize);
}

void clear_selected_fields(apg::ApgTransformContext* context) {
    if (context == nullptr) {
        return;
    }
    context->selected_field_count = 0;
    for (std::size_t i = 0; i < policy::kMaxFields; ++i) {
        context->selected_fields[i][0] = '\0';
    }
    context->request_query_view_ready = false;
    context->request_query_view = {};
}

std::size_t bounded_query_length(const apg::ApgTransformContext& context) {
    std::size_t length = 0;
    while (length < context.raw_query_length &&
           length < apg::ApgTransformContext::kRawQueryBufferSize &&
           context.raw_query[length] != '\0') {
        length += 1;
    }
    return length;
}

bool query_key_is_fields(const char* key, std::size_t key_length) {
    if (key == nullptr || key_length != 6) {
        return false;
    }
    return key[0] == 'f' && key[1] == 'i' && key[2] == 'e' && key[3] == 'l' && key[4] == 'd' &&
           key[5] == 's';
}

void parse_fields_value(const char* value, std::size_t value_length,
                        ParsedFieldsQuery* parsed_fields) {
    std::size_t token_start = 0;
    while (token_start <= value_length) {
        std::size_t token_end = token_start;
        while (token_end < value_length && value[token_end] != ',') {
            token_end += 1;
        }

        if (token_end > token_start) {
            if (parsed_fields->field_count >= policy::kMaxFields) {
                return;
            }

            const std::size_t destination_index = parsed_fields->field_count;
            const std::size_t token_length = token_end - token_start;
            const std::size_t copied_length =
                bounded_copy(value + token_start, token_length,
                             parsed_fields->fields[destination_index], policy::kMaxFieldNameLen);
            if (copied_length > 0) {
                parsed_fields->field_count += 1;
            }
        }

        if (token_end >= value_length) {
            return;
        }
        token_start = token_end + 1;
    }
}

} // namespace

bool extract_raw_path_and_query(const char* request_target, apg::ApgTransformContext* context) {
    if (context == nullptr) {
        return false;
    }

    if (request_target == nullptr || request_target[0] == '\0') {
        clear_path_and_query(context);
        return true;
    }

    std::size_t separator_index = 0;
    while (request_target[separator_index] != '\0' && request_target[separator_index] != '?') {
        separator_index += 1;
    }

    context->raw_path_length = bounded_copy(request_target, separator_index, context->raw_path,
                                            apg::ApgTransformContext::kRawPathBufferSize);

    if (request_target[separator_index] == '?') {
        const char* query_start = request_target + separator_index + 1;
        std::size_t query_length = 0;
        while (query_start[query_length] != '\0') {
            query_length += 1;
        }
        context->raw_query_length = bounded_copy(query_start, query_length, context->raw_query,
                                                 apg::ApgTransformContext::kRawQueryBufferSize);
        return true;
    }

    context->raw_query_length =
        bounded_copy("", 0, context->raw_query, apg::ApgTransformContext::kRawQueryBufferSize);
    return true;
}

bool parse_fields_query_parameter(const apg::RequestQueryView& query_view,
                                  ParsedFieldsQuery* parsed_fields) {
    if (parsed_fields == nullptr) {
        return false;
    }

    *parsed_fields = ParsedFieldsQuery{};
    for (std::uint8_t i = 0; i < query_view.count; ++i) {
        const auto& param = query_view.params[i];
        if (query_key_is_fields(param.key, param.key_len)) {
            if (param.value == nullptr) {
                return true;
            }
            parse_fields_value(param.value, param.value_len, parsed_fields);
            return true;
        }
    }
    return true;
}

bool parse_fields_query_parameter(const apg::ApgTransformContext& context,
                                  ParsedFieldsQuery* parsed_fields) {
    if (parsed_fields == nullptr) {
        return false;
    }
    if (context.request_query_view_ready) {
        return parse_fields_query_parameter(context.request_query_view, parsed_fields);
    }
    apg::RequestQueryView temp{};
    apg::parse_query_view(context.raw_query, context.raw_query_length, &temp);
    return parse_fields_query_parameter(temp, parsed_fields);
}

bool parse_and_store_selected_fields(apg::ApgTransformContext* context) {
    if (context == nullptr) {
        return false;
    }

    clear_selected_fields(context);
    context->client_query_present = false;

    if (!context->request_query_view_ready) {
        apg::parse_query_view(context->raw_query, context->raw_query_length,
                              &context->request_query_view);
        context->request_query_view_ready = true;
    }

    for (std::uint8_t i = 0; i < context->request_query_view.count; ++i) {
        const auto& param = context->request_query_view.params[i];
        if (query_key_is_fields(param.key, param.key_len)) {
            context->client_query_present = true;
            break;
        }
    }

    ParsedFieldsQuery parsed_fields{};
    if (!parse_fields_query_parameter(*context, &parsed_fields)) {
        return false;
    }

    context->selected_field_count = parsed_fields.field_count;
    for (std::size_t i = 0; i < parsed_fields.field_count; ++i) {
        const std::size_t token_length =
            bounded_copy(parsed_fields.fields[i], policy::kMaxFieldNameLen - 1,
                         context->selected_fields[i], policy::kMaxFieldNameLen);
        (void) token_length;
    }
    return true;
}

bool enforce_selected_fields_policy(apg::ApgTransformContext* context,
                                    const policy::FieldFilterPolicy& policy) {
    if (context == nullptr) {
        return false;
    }

    std::size_t write_index = 0;
    const std::size_t original_count = context->selected_field_count;
    for (std::size_t i = 0; i < original_count; ++i) {
        if (!policy::apply_field_filter(policy, context->selected_fields[i])) {
            continue;
        }
        if (write_index != i) {
            const std::size_t copied_length =
                bounded_copy(context->selected_fields[i], policy::kMaxFieldNameLen - 1,
                             context->selected_fields[write_index], policy::kMaxFieldNameLen);
            (void) copied_length;
        }
        write_index += 1;
    }

    for (std::size_t i = write_index; i < policy::kMaxFields; ++i) {
        context->selected_fields[i][0] = '\0';
    }
    context->selected_field_count = write_index;
    return true;
}

} // namespace bytetaper::field_selection

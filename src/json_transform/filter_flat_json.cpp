// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "field_selection/request_target.h"
#include "json_transform/content_type.h"

#include <cstddef>
#include <cstring>

namespace bytetaper::json_transform {

namespace {

constexpr const char* kInvalidJsonSafeErrorBody = R"({"error":"invalid_json"})";
constexpr std::size_t kMaxSelectionPathLen = 512;

struct FieldCountMetrics {
    std::size_t encountered_field_count = 0;
    std::size_t emitted_field_count = 0;
};

void assign_removed_field_count(const apg::ApgTransformContext& context,
                                const FieldCountMetrics& metrics) {
    if (metrics.encountered_field_count >= metrics.emitted_field_count) {
        context.removed_field_count = metrics.encountered_field_count - metrics.emitted_field_count;
        return;
    }
    context.removed_field_count = 0;
}

struct BoundedWriter {
    char* output = nullptr;
    std::size_t capacity = 0;
    std::size_t length = 0;

    void initialize() {
        if (output != nullptr && capacity > 0) {
            output[0] = '\0';
        }
    }

    void append_char(char ch) {
        if (output != nullptr && capacity > 0 && length + 1 < capacity) {
            output[length] = ch;
        }
        length += 1;
        if (output != nullptr && capacity > 0) {
            const std::size_t terminator_index = (length < capacity) ? length : (capacity - 1);
            output[terminator_index] = '\0';
        }
    }

    void append_cstr(const char* text) {
        if (text == nullptr) {
            return;
        }
        std::size_t index = 0;
        while (text[index] != '\0') {
            append_char(text[index]);
            index += 1;
        }
    }

    void append_slice(const char* text, std::size_t begin, std::size_t end) {
        for (std::size_t index = begin; index < end; ++index) {
            append_char(text[index]);
        }
    }

    bool fits_capacity() const {
        return capacity > 0 && length + 1 <= capacity;
    }

    std::size_t checkpoint() const {
        return length;
    }

    void rollback(std::size_t checkpoint_length) {
        length = checkpoint_length;
        if (output != nullptr && capacity > 0) {
            const std::size_t terminator_index = (length < capacity) ? length : (capacity - 1);
            output[terminator_index] = '\0';
        }
    }
};

bool is_field_selected(const apg::ApgTransformContext& context, const char* key) {
    if (key == nullptr) {
        return false;
    }

    const std::size_t selected_count = (context.selected_field_count <= policy::kMaxFields)
                                           ? context.selected_field_count
                                           : policy::kMaxFields;
    for (std::size_t index = 0; index < selected_count; ++index) {
        if (std::strcmp(context.selected_fields[index], key) == 0) {
            return true;
        }
    }
    return false;
}

bool selection_contains_unsupported_array_notation(const apg::ApgTransformContext& context) {
    const std::size_t selected_count = (context.selected_field_count <= policy::kMaxFields)
                                           ? context.selected_field_count
                                           : policy::kMaxFields;
    for (std::size_t index = 0; index < selected_count; ++index) {
        const char* selected = context.selected_fields[index];
        if (selected == nullptr) {
            continue;
        }
        for (std::size_t ch_index = 0; selected[ch_index] != '\0'; ++ch_index) {
            if (selected[ch_index] != '[') {
                if (selected[ch_index] == ']') {
                    return true;
                }
                continue;
            }
            if (selected[ch_index + 1] != ']') {
                return true;
            }
            ch_index += 1;
        }
    }
    return false;
}

bool is_whitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

void skip_whitespace(const char* body, std::size_t length, std::size_t* index) {
    while (*index < length && is_whitespace(body[*index])) {
        *index += 1;
    }
}

bool is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

bool parse_json_string_token(const char* body, std::size_t length, std::size_t* index,
                             std::size_t* token_begin, std::size_t* token_end, char* decoded_key,
                             std::size_t decoded_key_capacity, std::size_t* decoded_key_length) {
    if (*index >= length || body[*index] != '"') {
        return false;
    }

    if (token_begin != nullptr) {
        *token_begin = *index;
    }
    *index += 1;

    std::size_t written = 0;
    while (*index < length) {
        const char ch = body[*index];
        if (ch == '"') {
            *index += 1;
            if (token_end != nullptr) {
                *token_end = *index;
            }
            if (decoded_key != nullptr && decoded_key_capacity > 0) {
                const std::size_t term_index =
                    (written < decoded_key_capacity) ? written : (decoded_key_capacity - 1);
                decoded_key[term_index] = '\0';
            }
            if (decoded_key_length != nullptr) {
                *decoded_key_length = (decoded_key_capacity > 0 && written >= decoded_key_capacity)
                                          ? (decoded_key_capacity - 1)
                                          : written;
            }
            return true;
        }
        if (static_cast<unsigned char>(ch) < 0x20) {
            return false;
        }
        if (ch == '\\') {
            *index += 1;
            if (*index >= length) {
                return false;
            }
            const char escaped = body[*index];
            if (decoded_key != nullptr && decoded_key_capacity > 0 &&
                written + 1 < decoded_key_capacity) {
                decoded_key[written] = escaped;
            }
            written += 1;
            *index += 1;
            continue;
        }
        if (decoded_key != nullptr && decoded_key_capacity > 0 &&
            written + 1 < decoded_key_capacity) {
            decoded_key[written] = ch;
        }
        written += 1;
        *index += 1;
    }
    return false;
}

bool parse_number_token(const char* body, std::size_t length, std::size_t* index) {
    if (*index >= length) {
        return false;
    }
    if (body[*index] == '-') {
        *index += 1;
        if (*index >= length) {
            return false;
        }
    }
    if (body[*index] == '0') {
        *index += 1;
    } else if (is_digit(body[*index])) {
        while (*index < length && is_digit(body[*index])) {
            *index += 1;
        }
    } else {
        return false;
    }
    if (*index < length && body[*index] == '.') {
        *index += 1;
        if (*index >= length || !is_digit(body[*index])) {
            return false;
        }
        while (*index < length && is_digit(body[*index])) {
            *index += 1;
        }
    }
    if (*index < length && (body[*index] == 'e' || body[*index] == 'E')) {
        *index += 1;
        if (*index < length && (body[*index] == '+' || body[*index] == '-')) {
            *index += 1;
        }
        if (*index >= length || !is_digit(body[*index])) {
            return false;
        }
        while (*index < length && is_digit(body[*index])) {
            *index += 1;
        }
    }
    return true;
}

bool parse_literal_token(const char* body, std::size_t length, std::size_t* index,
                         const char* literal) {
    std::size_t local = *index;
    std::size_t literal_index = 0;
    while (literal[literal_index] != '\0') {
        if (local >= length || body[local] != literal[literal_index]) {
            return false;
        }
        local += 1;
        literal_index += 1;
    }
    *index = local;
    return true;
}

FlatJsonFilterStatus consume_json_value_any(const char* body, std::size_t length,
                                            std::size_t* index, FieldCountMetrics* metrics);

FlatJsonFilterStatus consume_object_any(const char* body, std::size_t length, std::size_t* index,
                                        FieldCountMetrics* metrics) {
    if (*index >= length || body[*index] != '{') {
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    *index += 1;
    skip_whitespace(body, length, index);
    if (*index < length && body[*index] == '}') {
        *index += 1;
        return FlatJsonFilterStatus::Ok;
    }

    while (*index < length) {
        if (!parse_json_string_token(body, length, index, nullptr, nullptr, nullptr, 0, nullptr)) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        skip_whitespace(body, length, index);
        if (*index >= length || body[*index] != ':') {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        *index += 1;
        skip_whitespace(body, length, index);
        if (metrics != nullptr) {
            metrics->encountered_field_count += 1;
        }
        const FlatJsonFilterStatus value_status =
            consume_json_value_any(body, length, index, metrics);
        if (value_status != FlatJsonFilterStatus::Ok) {
            return value_status;
        }
        skip_whitespace(body, length, index);
        if (*index >= length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        if (body[*index] == ',') {
            *index += 1;
            skip_whitespace(body, length, index);
            continue;
        }
        if (body[*index] == '}') {
            *index += 1;
            return FlatJsonFilterStatus::Ok;
        }
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    return FlatJsonFilterStatus::SkipUnsupported;
}

FlatJsonFilterStatus consume_array_any(const char* body, std::size_t length, std::size_t* index,
                                       FieldCountMetrics* metrics) {
    if (*index >= length || body[*index] != '[') {
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    *index += 1;
    skip_whitespace(body, length, index);
    if (*index < length && body[*index] == ']') {
        *index += 1;
        return FlatJsonFilterStatus::Ok;
    }

    while (*index < length) {
        const FlatJsonFilterStatus element_status =
            consume_json_value_any(body, length, index, metrics);
        if (element_status != FlatJsonFilterStatus::Ok) {
            return element_status;
        }
        skip_whitespace(body, length, index);
        if (*index >= length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        if (body[*index] == ',') {
            *index += 1;
            skip_whitespace(body, length, index);
            continue;
        }
        if (body[*index] == ']') {
            *index += 1;
            return FlatJsonFilterStatus::Ok;
        }
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    return FlatJsonFilterStatus::SkipUnsupported;
}

FlatJsonFilterStatus consume_json_value_any(const char* body, std::size_t length,
                                            std::size_t* index, FieldCountMetrics* metrics) {
    if (*index >= length) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    const char ch = body[*index];
    if (ch == '{') {
        return consume_object_any(body, length, index, metrics);
    }
    if (ch == '[') {
        return consume_array_any(body, length, index, metrics);
    }
    if (ch == '"') {
        if (!parse_json_string_token(body, length, index, nullptr, nullptr, nullptr, 0, nullptr)) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        return FlatJsonFilterStatus::Ok;
    }
    if (ch == 't') {
        return parse_literal_token(body, length, index, "true")
                   ? FlatJsonFilterStatus::Ok
                   : FlatJsonFilterStatus::SkipUnsupported;
    }
    if (ch == 'f') {
        return parse_literal_token(body, length, index, "false")
                   ? FlatJsonFilterStatus::Ok
                   : FlatJsonFilterStatus::SkipUnsupported;
    }
    if (ch == 'n') {
        return parse_literal_token(body, length, index, "null")
                   ? FlatJsonFilterStatus::Ok
                   : FlatJsonFilterStatus::SkipUnsupported;
    }
    if (ch == '-' || is_digit(ch)) {
        return parse_number_token(body, length, index) ? FlatJsonFilterStatus::Ok
                                                       : FlatJsonFilterStatus::SkipUnsupported;
    }
    return FlatJsonFilterStatus::SkipUnsupported;
}

FlatJsonFilterStatus consume_json_value_object_only(const char* body, std::size_t length,
                                                    std::size_t* index,
                                                    FieldCountMetrics* metrics) {
    if (*index < length && body[*index] == '[') {
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    return consume_json_value_any(body, length, index, metrics);
}

bool build_full_path(const char* prefix, const char* key, char* output, std::size_t capacity) {
    if (output == nullptr || capacity == 0 || key == nullptr || key[0] == '\0') {
        return false;
    }

    std::size_t index = 0;
    if (prefix != nullptr && prefix[0] != '\0') {
        while (prefix[index] != '\0' && index + 1 < capacity) {
            output[index] = prefix[index];
            index += 1;
        }
        if (prefix[index] != '\0' || index + 1 >= capacity) {
            return false;
        }
        output[index] = '.';
        index += 1;
    }
    std::size_t key_index = 0;
    while (key[key_index] != '\0' && index + 1 < capacity) {
        output[index] = key[key_index];
        index += 1;
        key_index += 1;
    }
    if (key[key_index] != '\0') {
        return false;
    }
    output[index] = '\0';
    return true;
}

bool build_array_element_path(const char* array_path, char* output, std::size_t capacity) {
    if (array_path == nullptr || output == nullptr || capacity == 0) {
        return false;
    }

    std::size_t index = 0;
    while (array_path[index] != '\0' && index + 1 < capacity) {
        output[index] = array_path[index];
        index += 1;
    }
    if (array_path[index] != '\0' || index + 3 > capacity) {
        return false;
    }
    output[index] = '[';
    output[index + 1] = ']';
    output[index + 2] = '\0';
    return true;
}

FlatJsonFilterStatus
filter_nested_object_value(const char* body, std::size_t length, std::size_t* index,
                           const apg::ApgTransformContext& context,
                           const bytetaper::field_selection::CompiledFieldSelection& sel,
                           const char* prefix, BoundedWriter* writer,
                           std::size_t* emitted_field_count, FieldCountMetrics* metrics);

FlatJsonFilterStatus
filter_nested_array_value(const char* body, std::size_t length, std::size_t* index,
                          const apg::ApgTransformContext& context,
                          const bytetaper::field_selection::CompiledFieldSelection& sel,
                          const char* array_path, BoundedWriter* writer,
                          std::size_t* emitted_element_count, FieldCountMetrics* metrics);

FlatJsonFilterStatus
write_filtered_object(const char* body, std::size_t length, std::size_t* index,
                      const apg::ApgTransformContext& context,
                      const bytetaper::field_selection::CompiledFieldSelection& sel,
                      const char* prefix, BoundedWriter* writer, std::size_t* emitted_field_count,
                      FieldCountMetrics* metrics) {
    if (*index >= length || body[*index] != '{' || writer == nullptr ||
        emitted_field_count == nullptr) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    *emitted_field_count = 0;
    writer->append_char('{');
    *index += 1;
    skip_whitespace(body, length, index);
    if (*index < length && body[*index] == '}') {
        *index += 1;
        writer->append_char('}');
        return FlatJsonFilterStatus::Ok;
    }

    bool first_emitted = true;
    while (*index < length) {
        std::size_t key_token_begin = 0;
        std::size_t key_token_end = 0;
        char key[policy::kMaxFieldNameLen] = {};
        std::size_t key_length = 0;
        if (!parse_json_string_token(body, length, index, &key_token_begin, &key_token_end, key,
                                     policy::kMaxFieldNameLen, &key_length)) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        (void) key_length;
        if (metrics != nullptr) {
            metrics->encountered_field_count += 1;
        }

        skip_whitespace(body, length, index);
        if (*index >= length || body[*index] != ':') {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        *index += 1;
        skip_whitespace(body, length, index);

        char full_path[kMaxSelectionPathLen] = {};
        if (!build_full_path(prefix, key, full_path, sizeof(full_path))) {
            // Path too long for selection matching; consume safely and ignore field.
            const FlatJsonFilterStatus consume_status =
                consume_json_value_object_only(body, length, index, metrics);
            if (consume_status != FlatJsonFilterStatus::Ok) {
                return consume_status;
            }
        } else {
            const bytetaper::field_selection::SelectionMatchResult match =
                bytetaper::field_selection::match_selection_compiled(sel, full_path);

            const std::size_t value_start = *index;
            if (body[*index] == '{') {
                if (match.exact_selected) {
                    const std::size_t encountered_before =
                        (metrics != nullptr) ? metrics->encountered_field_count : 0;
                    const FlatJsonFilterStatus consume_status =
                        consume_object_any(body, length, index, metrics);
                    if (consume_status != FlatJsonFilterStatus::Ok) {
                        return consume_status;
                    }
                    const std::size_t value_end = *index;
                    if (!first_emitted) {
                        writer->append_char(',');
                    }
                    first_emitted = false;
                    writer->append_slice(body, key_token_begin, key_token_end);
                    writer->append_char(':');
                    writer->append_slice(body, value_start, value_end);
                    *emitted_field_count += 1;
                    if (metrics != nullptr) {
                        metrics->emitted_field_count += 1;
                        const std::size_t nested_encountered =
                            metrics->encountered_field_count - encountered_before;
                        metrics->emitted_field_count += nested_encountered;
                    }
                } else if (match.has_selected_descendant) {
                    const std::size_t member_checkpoint = writer->checkpoint();
                    if (!first_emitted) {
                        writer->append_char(',');
                    }
                    writer->append_slice(body, key_token_begin, key_token_end);
                    writer->append_char(':');

                    std::size_t nested_emitted_count = 0;
                    const FlatJsonFilterStatus nested_status =
                        filter_nested_object_value(body, length, index, context, sel, full_path,
                                                   writer, &nested_emitted_count, metrics);
                    if (nested_status != FlatJsonFilterStatus::Ok) {
                        return nested_status;
                    }
                    if (nested_emitted_count == 0) {
                        writer->rollback(member_checkpoint);
                    } else {
                        first_emitted = false;
                        *emitted_field_count += 1;
                        if (metrics != nullptr) {
                            metrics->emitted_field_count += 1;
                        }
                    }
                } else {
                    const FlatJsonFilterStatus consume_status =
                        consume_object_any(body, length, index, metrics);
                    if (consume_status != FlatJsonFilterStatus::Ok) {
                        return consume_status;
                    }
                }
            } else if (body[*index] == '[') {
                char array_element_path[kMaxSelectionPathLen] = {};
                if (!build_array_element_path(full_path, array_element_path,
                                              sizeof(array_element_path))) {
                    const FlatJsonFilterStatus consume_status =
                        consume_array_any(body, length, index, metrics);
                    if (consume_status != FlatJsonFilterStatus::Ok) {
                        return consume_status;
                    }
                } else {
                    const bytetaper::field_selection::SelectionMatchResult element_match =
                        bytetaper::field_selection::match_selection_compiled(sel,
                                                                             array_element_path);
                    if (match.exact_selected || element_match.exact_selected ||
                        element_match.has_selected_descendant) {
                        const std::size_t member_checkpoint = writer->checkpoint();
                        if (!first_emitted) {
                            writer->append_char(',');
                        }
                        writer->append_slice(body, key_token_begin, key_token_end);
                        writer->append_char(':');

                        std::size_t emitted_elements = 0;
                        const FlatJsonFilterStatus array_status =
                            filter_nested_array_value(body, length, index, context, sel, full_path,
                                                      writer, &emitted_elements, metrics);
                        if (array_status != FlatJsonFilterStatus::Ok) {
                            return array_status;
                        }
                        if (match.exact_selected || emitted_elements > 0) {
                            first_emitted = false;
                            *emitted_field_count += 1;
                            if (metrics != nullptr) {
                                metrics->emitted_field_count += 1;
                            }
                        } else {
                            writer->rollback(member_checkpoint);
                        }
                    } else {
                        const FlatJsonFilterStatus consume_status =
                            consume_array_any(body, length, index, metrics);
                        if (consume_status != FlatJsonFilterStatus::Ok) {
                            return consume_status;
                        }
                    }
                }
            } else {
                const std::size_t primitive_start = *index;
                const FlatJsonFilterStatus primitive_status =
                    consume_json_value_object_only(body, length, index, metrics);
                if (primitive_status != FlatJsonFilterStatus::Ok) {
                    return primitive_status;
                }
                const std::size_t primitive_end = *index;
                if (match.exact_selected) {
                    if (!first_emitted) {
                        writer->append_char(',');
                    }
                    first_emitted = false;
                    writer->append_slice(body, key_token_begin, key_token_end);
                    writer->append_char(':');
                    writer->append_slice(body, primitive_start, primitive_end);
                    *emitted_field_count += 1;
                    if (metrics != nullptr) {
                        metrics->emitted_field_count += 1;
                    }
                }
            }
        }

        skip_whitespace(body, length, index);
        if (*index >= length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        if (body[*index] == ',') {
            *index += 1;
            skip_whitespace(body, length, index);
            continue;
        }
        if (body[*index] == '}') {
            *index += 1;
            writer->append_char('}');
            return FlatJsonFilterStatus::Ok;
        }
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    return FlatJsonFilterStatus::SkipUnsupported;
}

FlatJsonFilterStatus
filter_nested_array_value(const char* body, std::size_t length, std::size_t* index,
                          const apg::ApgTransformContext& context,
                          const bytetaper::field_selection::CompiledFieldSelection& sel,
                          const char* array_path, BoundedWriter* writer,
                          std::size_t* emitted_element_count, FieldCountMetrics* metrics) {
    if (*index >= length || body[*index] != '[' || writer == nullptr ||
        emitted_element_count == nullptr) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    *emitted_element_count = 0;
    writer->append_char('[');
    *index += 1;
    skip_whitespace(body, length, index);
    if (*index < length && body[*index] == ']') {
        *index += 1;
        writer->append_char(']');
        return FlatJsonFilterStatus::Ok;
    }

    char element_path[kMaxSelectionPathLen] = {};
    if (!build_array_element_path(array_path, element_path, sizeof(element_path))) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    const bytetaper::field_selection::SelectionMatchResult element_match =
        bytetaper::field_selection::match_selection_compiled(sel, element_path);

    bool first_emitted = true;
    while (*index < length) {
        if (body[*index] == '{') {
            const std::size_t element_checkpoint = writer->checkpoint();
            if (!first_emitted) {
                writer->append_char(',');
            }

            if (element_match.exact_selected && !element_match.has_selected_descendant) {
                const std::size_t copy_start = *index;
                const std::size_t encountered_before =
                    (metrics != nullptr) ? metrics->encountered_field_count : 0;
                const FlatJsonFilterStatus consume_status =
                    consume_object_any(body, length, index, metrics);
                if (consume_status != FlatJsonFilterStatus::Ok) {
                    return consume_status;
                }
                writer->append_slice(body, copy_start, *index);
                first_emitted = false;
                *emitted_element_count += 1;
                if (metrics != nullptr) {
                    const std::size_t nested_encountered =
                        metrics->encountered_field_count - encountered_before;
                    metrics->emitted_field_count += nested_encountered;
                }
            } else if (element_match.has_selected_descendant) {
                std::size_t nested_fields = 0;
                const FlatJsonFilterStatus nested_status =
                    filter_nested_object_value(body, length, index, context, sel, element_path,
                                               writer, &nested_fields, metrics);
                if (nested_status != FlatJsonFilterStatus::Ok) {
                    return nested_status;
                }
                if (nested_fields == 0) {
                    writer->rollback(element_checkpoint);
                } else {
                    first_emitted = false;
                    *emitted_element_count += 1;
                }
            } else {
                writer->rollback(element_checkpoint);
                const FlatJsonFilterStatus consume_status =
                    consume_object_any(body, length, index, metrics);
                if (consume_status != FlatJsonFilterStatus::Ok) {
                    return consume_status;
                }
            }
        } else {
            // Object-array filtering only in this phase: skip non-object elements safely.
            const FlatJsonFilterStatus consume_status =
                consume_json_value_any(body, length, index, metrics);
            if (consume_status != FlatJsonFilterStatus::Ok) {
                return consume_status;
            }
        }

        skip_whitespace(body, length, index);
        if (*index >= length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        if (body[*index] == ',') {
            *index += 1;
            skip_whitespace(body, length, index);
            continue;
        }
        if (body[*index] == ']') {
            *index += 1;
            writer->append_char(']');
            return FlatJsonFilterStatus::Ok;
        }
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    return FlatJsonFilterStatus::SkipUnsupported;
}

FlatJsonFilterStatus
filter_nested_object_value(const char* body, std::size_t length, std::size_t* index,
                           const apg::ApgTransformContext& context,
                           const bytetaper::field_selection::CompiledFieldSelection& sel,
                           const char* prefix, BoundedWriter* writer,
                           std::size_t* emitted_field_count, FieldCountMetrics* metrics) {
    return write_filtered_object(body, length, index, context, sel, prefix, writer,
                                 emitted_field_count, metrics);
}

FlatJsonFilterStatus filter_nested_json_by_selected_fields(
    const char* input_body, const apg::ApgTransformContext& context, char* output,
    std::size_t output_capacity, std::size_t* output_length, FieldCountMetrics* out_metrics) {
    if (input_body == nullptr || output == nullptr || output_length == nullptr) {
        return FlatJsonFilterStatus::InvalidInput;
    }

    *output_length = 0;
    if (out_metrics != nullptr) {
        out_metrics->encountered_field_count = 0;
        out_metrics->emitted_field_count = 0;
    }
    BoundedWriter writer{ output, output_capacity, 0 };
    writer.initialize();

    if (selection_contains_unsupported_array_notation(context)) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    std::size_t length = 0;
    while (input_body[length] != '\0') {
        length += 1;
    }

    std::size_t index = 0;
    skip_whitespace(input_body, length, &index);
    if (index >= length || input_body[index] != '{') {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    bytetaper::field_selection::CompiledFieldSelection sel;
    bytetaper::field_selection::compile_field_selection(context, &sel);

    std::size_t emitted_count = 0;
    const FlatJsonFilterStatus status = write_filtered_object(
        input_body, length, &index, context, sel, "", &writer, &emitted_count, out_metrics);
    if (status != FlatJsonFilterStatus::Ok) {
        return status;
    }

    skip_whitespace(input_body, length, &index);
    if (index != length) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    *output_length = writer.length;
    return writer.fits_capacity() ? FlatJsonFilterStatus::Ok : FlatJsonFilterStatus::OutputTooSmall;
}

FlatJsonFilterStatus copy_original_body(const char* input_body, char* output,
                                        std::size_t output_capacity, std::size_t* output_length) {
    if (input_body == nullptr || output == nullptr || output_length == nullptr) {
        return FlatJsonFilterStatus::InvalidInput;
    }

    *output_length = 0;
    BoundedWriter writer{ output, output_capacity, 0 };
    writer.initialize();

    std::size_t index = 0;
    while (input_body[index] != '\0') {
        writer.append_char(input_body[index]);
        index += 1;
    }

    *output_length = writer.length;
    return writer.fits_capacity() ? FlatJsonFilterStatus::Ok : FlatJsonFilterStatus::OutputTooSmall;
}

FlatJsonFilterStatus write_invalid_json_safe_error(char* output, std::size_t output_capacity,
                                                   std::size_t* output_length) {
    if (output == nullptr || output_length == nullptr) {
        return FlatJsonFilterStatus::InvalidInput;
    }

    *output_length = 0;
    BoundedWriter writer{ output, output_capacity, 0 };
    writer.initialize();

    std::size_t index = 0;
    while (kInvalidJsonSafeErrorBody[index] != '\0') {
        writer.append_char(kInvalidJsonSafeErrorBody[index]);
        index += 1;
    }

    *output_length = writer.length;
    if (!writer.fits_capacity()) {
        return FlatJsonFilterStatus::OutputTooSmall;
    }
    return FlatJsonFilterStatus::InvalidJsonSafeError;
}

} // namespace

FlatJsonFilterStatus filter_flat_json_by_selected_fields(const ParsedFlatJsonObject& parsed,
                                                         const apg::ApgTransformContext& context,
                                                         char* output, std::size_t output_capacity,
                                                         std::size_t* output_length) {
    context.removed_field_count = 0;
    if (output == nullptr || output_length == nullptr) {
        return FlatJsonFilterStatus::InvalidInput;
    }

    *output_length = 0;
    BoundedWriter writer{ output, output_capacity, 0 };
    writer.initialize();

    if (parsed.source == nullptr) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    FieldCountMetrics metrics{};
    writer.append_char('{');
    bool first_emitted = true;

    const std::size_t parsed_field_count =
        (parsed.field_count <= policy::kMaxFields) ? parsed.field_count : policy::kMaxFields;
    for (std::size_t field_index = 0; field_index < parsed_field_count; ++field_index) {
        metrics.encountered_field_count += 1;
        const FlatJsonFieldView& field = parsed.fields[field_index];
        if (!is_field_selected(context, field.key)) {
            continue;
        }
        if (field.value_begin > field.value_end || field.value_end > parsed.source_length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }

        if (!first_emitted) {
            writer.append_char(',');
        }
        first_emitted = false;

        writer.append_char('"');
        writer.append_cstr(field.key);
        writer.append_cstr("\":");
        writer.append_slice(parsed.source, field.value_begin, field.value_end);
        metrics.emitted_field_count += 1;
    }

    writer.append_char('}');
    *output_length = writer.length;
    assign_removed_field_count(context, metrics);
    return writer.fits_capacity() ? FlatJsonFilterStatus::Ok : FlatJsonFilterStatus::OutputTooSmall;
}

static FlatJsonFilterStatus
write_filtered_object_denylist(const char* body, std::size_t length, std::size_t* index,
                               const policy::FieldFilterPolicy& policy_filter, const char* prefix,
                               BoundedWriter* writer, std::size_t* emitted_field_count,
                               FieldCountMetrics* metrics);

static FlatJsonFilterStatus
filter_nested_array_value_denylist(const char* body, std::size_t length, std::size_t* index,
                                   const policy::FieldFilterPolicy& policy_filter,
                                   const char* array_path, BoundedWriter* writer,
                                   std::size_t* emitted_element_count, FieldCountMetrics* metrics) {
    if (*index >= length || body[*index] != '[' || writer == nullptr ||
        emitted_element_count == nullptr) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    *emitted_element_count = 0;
    writer->append_char('[');
    *index += 1;
    skip_whitespace(body, length, index);
    if (*index < length && body[*index] == ']') {
        *index += 1;
        writer->append_char(']');
        return FlatJsonFilterStatus::Ok;
    }

    char element_path[kMaxSelectionPathLen] = {};
    if (!build_array_element_path(array_path, element_path, sizeof(element_path))) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    const bool element_allowed = policy::apply_field_filter(policy_filter, element_path);

    bool first_emitted = true;
    while (*index < length) {
        if (body[*index] == '{') {
            const std::size_t element_checkpoint = writer->checkpoint();
            if (!first_emitted) {
                writer->append_char(',');
            }

            if (!element_allowed) {
                writer->rollback(element_checkpoint);
                const FlatJsonFilterStatus consume_status =
                    consume_object_any(body, length, index, metrics);
                if (consume_status != FlatJsonFilterStatus::Ok) {
                    return consume_status;
                }
            } else {
                std::size_t nested_fields = 0;
                const FlatJsonFilterStatus nested_status =
                    write_filtered_object_denylist(body, length, index, policy_filter, element_path,
                                                   writer, &nested_fields, metrics);
                if (nested_status != FlatJsonFilterStatus::Ok) {
                    return nested_status;
                }
                if (nested_fields == 0) {
                    writer->rollback(element_checkpoint);
                } else {
                    first_emitted = false;
                    *emitted_element_count += 1;
                }
            }
        } else {
            const std::size_t element_start = *index;
            const FlatJsonFilterStatus consume_status =
                consume_json_value_any(body, length, index, metrics);
            if (consume_status != FlatJsonFilterStatus::Ok) {
                return consume_status;
            }
            if (element_allowed) {
                if (!first_emitted) {
                    writer->append_char(',');
                }
                first_emitted = false;
                writer->append_slice(body, element_start, *index);
                *emitted_element_count += 1;
            }
        }

        skip_whitespace(body, length, index);
        if (*index >= length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        if (body[*index] == ',') {
            *index += 1;
            skip_whitespace(body, length, index);
            continue;
        }
        if (body[*index] == ']') {
            *index += 1;
            writer->append_char(']');
            return FlatJsonFilterStatus::Ok;
        }
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    return FlatJsonFilterStatus::SkipUnsupported;
}

static FlatJsonFilterStatus
write_filtered_object_denylist(const char* body, std::size_t length, std::size_t* index,
                               const policy::FieldFilterPolicy& policy_filter, const char* prefix,
                               BoundedWriter* writer, std::size_t* emitted_field_count,
                               FieldCountMetrics* metrics) {
    if (*index >= length || body[*index] != '{' || writer == nullptr ||
        emitted_field_count == nullptr) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    *emitted_field_count = 0;
    writer->append_char('{');
    *index += 1;
    skip_whitespace(body, length, index);
    if (*index < length && body[*index] == '}') {
        *index += 1;
        writer->append_char('}');
        return FlatJsonFilterStatus::Ok;
    }

    bool first_emitted = true;
    while (*index < length) {
        std::size_t key_token_begin = 0;
        std::size_t key_token_end = 0;
        char key[policy::kMaxFieldNameLen] = {};
        std::size_t key_length = 0;
        if (!parse_json_string_token(body, length, index, &key_token_begin, &key_token_end, key,
                                     policy::kMaxFieldNameLen, &key_length)) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        if (metrics != nullptr) {
            metrics->encountered_field_count += 1;
        }

        skip_whitespace(body, length, index);
        if (*index >= length || body[*index] != ':') {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        *index += 1;
        skip_whitespace(body, length, index);

        char full_path[kMaxSelectionPathLen] = {};
        if (!build_full_path(prefix, key, full_path, sizeof(full_path))) {
            const FlatJsonFilterStatus consume_status =
                consume_json_value_object_only(body, length, index, metrics);
            if (consume_status != FlatJsonFilterStatus::Ok) {
                return consume_status;
            }
        } else {
            const bool allowed = policy::apply_field_filter(policy_filter, full_path);

            if (!allowed) {
                const FlatJsonFilterStatus consume_status =
                    consume_json_value_any(body, length, index, metrics);
                if (consume_status != FlatJsonFilterStatus::Ok) {
                    return consume_status;
                }
            } else {
                const std::size_t value_start = *index;
                if (body[*index] == '{') {
                    const std::size_t member_checkpoint = writer->checkpoint();
                    if (!first_emitted) {
                        writer->append_char(',');
                    }
                    writer->append_slice(body, key_token_begin, key_token_end);
                    writer->append_char(':');

                    std::size_t nested_emitted_count = 0;
                    const FlatJsonFilterStatus nested_status = write_filtered_object_denylist(
                        body, length, index, policy_filter, full_path, writer,
                        &nested_emitted_count, metrics);
                    if (nested_status != FlatJsonFilterStatus::Ok) {
                        return nested_status;
                    }
                    if (nested_emitted_count == 0) {
                        writer->rollback(member_checkpoint);
                    } else {
                        first_emitted = false;
                        *emitted_field_count += 1;
                        if (metrics != nullptr) {
                            metrics->emitted_field_count += 1;
                        }
                    }
                } else if (body[*index] == '[') {
                    char array_element_path[kMaxSelectionPathLen] = {};
                    if (!build_array_element_path(full_path, array_element_path,
                                                  sizeof(array_element_path))) {
                        const FlatJsonFilterStatus consume_status =
                            consume_array_any(body, length, index, metrics);
                        if (consume_status != FlatJsonFilterStatus::Ok) {
                            return consume_status;
                        }
                    } else {
                        const std::size_t member_checkpoint = writer->checkpoint();
                        if (!first_emitted) {
                            writer->append_char(',');
                        }
                        writer->append_slice(body, key_token_begin, key_token_end);
                        writer->append_char(':');

                        std::size_t emitted_elements = 0;
                        const FlatJsonFilterStatus array_status =
                            filter_nested_array_value_denylist(body, length, index, policy_filter,
                                                               full_path, writer, &emitted_elements,
                                                               metrics);
                        if (array_status != FlatJsonFilterStatus::Ok) {
                            return array_status;
                        }
                        if (emitted_elements > 0) {
                            first_emitted = false;
                            *emitted_field_count += 1;
                            if (metrics != nullptr) {
                                metrics->emitted_field_count += 1;
                            }
                        } else {
                            writer->rollback(member_checkpoint);
                        }
                    }
                } else {
                    const std::size_t primitive_start = *index;
                    const FlatJsonFilterStatus primitive_status =
                        consume_json_value_object_only(body, length, index, metrics);
                    if (primitive_status != FlatJsonFilterStatus::Ok) {
                        return primitive_status;
                    }
                    const std::size_t primitive_end = *index;
                    if (!first_emitted) {
                        writer->append_char(',');
                    }
                    first_emitted = false;
                    writer->append_slice(body, key_token_begin, key_token_end);
                    writer->append_char(':');
                    writer->append_slice(body, primitive_start, primitive_end);
                    *emitted_field_count += 1;
                    if (metrics != nullptr) {
                        metrics->emitted_field_count += 1;
                    }
                }
            }
        }

        skip_whitespace(body, length, index);
        if (*index >= length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
        if (body[*index] == ',') {
            *index += 1;
            skip_whitespace(body, length, index);
            continue;
        }
        if (body[*index] == '}') {
            *index += 1;
            writer->append_char('}');
            return FlatJsonFilterStatus::Ok;
        }
        return FlatJsonFilterStatus::SkipUnsupported;
    }
    return FlatJsonFilterStatus::SkipUnsupported;
}

static FlatJsonFilterStatus filter_nested_json_by_policy_denylist(
    const char* input_body, const policy::FieldFilterPolicy& policy_filter, char* output,
    std::size_t output_capacity, std::size_t* output_length, FieldCountMetrics* out_metrics) {
    if (input_body == nullptr || output == nullptr || output_length == nullptr) {
        return FlatJsonFilterStatus::InvalidInput;
    }

    *output_length = 0;
    if (out_metrics != nullptr) {
        out_metrics->encountered_field_count = 0;
        out_metrics->emitted_field_count = 0;
    }
    BoundedWriter writer{ output, output_capacity, 0 };
    writer.initialize();

    std::size_t length = 0;
    while (input_body[length] != '\0') {
        length += 1;
    }

    std::size_t index = 0;
    skip_whitespace(input_body, length, &index);
    if (index >= length || input_body[index] != '{') {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    std::size_t emitted_count = 0;
    const FlatJsonFilterStatus status = write_filtered_object_denylist(
        input_body, length, &index, policy_filter, "", &writer, &emitted_count, out_metrics);
    if (status != FlatJsonFilterStatus::Ok) {
        return status;
    }

    skip_whitespace(input_body, length, &index);
    if (index != length) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    *output_length = writer.length;
    return writer.fits_capacity() ? FlatJsonFilterStatus::Ok : FlatJsonFilterStatus::OutputTooSmall;
}

FlatJsonFilterStatus
filter_flat_json_by_policy_denylist(const ParsedFlatJsonObject& parsed,
                                    const apg::ApgTransformContext& context,
                                    const policy::FieldFilterPolicy& policy_filter, char* output,
                                    std::size_t output_capacity, std::size_t* output_length) {
    context.removed_field_count = 0;
    if (output == nullptr || output_length == nullptr) {
        return FlatJsonFilterStatus::InvalidInput;
    }

    *output_length = 0;
    BoundedWriter writer{ output, output_capacity, 0 };
    writer.initialize();

    if (parsed.source == nullptr) {
        return FlatJsonFilterStatus::SkipUnsupported;
    }

    FieldCountMetrics metrics{};
    writer.append_char('{');
    bool first_emitted = true;

    const std::size_t parsed_field_count =
        (parsed.field_count <= policy::kMaxFields) ? parsed.field_count : policy::kMaxFields;
    for (std::size_t field_index = 0; field_index < parsed_field_count; ++field_index) {
        metrics.encountered_field_count += 1;
        const FlatJsonFieldView& field = parsed.fields[field_index];
        if (!policy::apply_field_filter(policy_filter, field.key)) {
            continue;
        }
        if (field.value_begin > field.value_end || field.value_end > parsed.source_length) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }

        if (!first_emitted) {
            writer.append_char(',');
        }
        first_emitted = false;

        writer.append_char('"');
        writer.append_cstr(field.key);
        writer.append_cstr("\":");
        writer.append_slice(parsed.source, field.value_begin, field.value_end);
        metrics.emitted_field_count += 1;
    }

    writer.append_char('}');
    *output_length = writer.length;
    assign_removed_field_count(context, metrics);
    return writer.fits_capacity() ? FlatJsonFilterStatus::Ok : FlatJsonFilterStatus::OutputTooSmall;
}

FlatJsonFilterStatus transform_flat_json_with_filter_toggle(const char* input_body,
                                                            const ParsedFlatJsonObject* parsed,
                                                            const apg::ApgTransformContext& context,
                                                            bool filtering_enabled, char* output,
                                                            std::size_t output_capacity,
                                                            std::size_t* output_length) {
    context.removed_field_count = 0;
    if (parsed == nullptr) {
        return transform_flat_json_with_filter_toggle(input_body, FlatJsonParseStatus::InvalidInput,
                                                      parsed, context, filtering_enabled, output,
                                                      output_capacity, output_length);
    }
    const FlatJsonParseStatus inferred_status = (parsed->source == nullptr)
                                                    ? FlatJsonParseStatus::SkipUnsupported
                                                    : FlatJsonParseStatus::Ok;
    return transform_flat_json_with_filter_toggle(input_body, inferred_status, parsed, context,
                                                  filtering_enabled, output, output_capacity,
                                                  output_length);
}

FlatJsonFilterStatus transform_flat_json_with_filter_toggle(
    const char* input_body, FlatJsonParseStatus parse_status, const ParsedFlatJsonObject* parsed,
    const apg::ApgTransformContext& context, bool filtering_enabled, char* output,
    std::size_t output_capacity, std::size_t* output_length) {
    context.removed_field_count = 0;
    if (!filtering_enabled) {
        return copy_original_body(input_body, output, output_capacity, output_length);
    }

    if (parse_status == FlatJsonParseStatus::InvalidJson) {
        return write_invalid_json_safe_error(output, output_capacity, output_length);
    }

    if (parse_status == FlatJsonParseStatus::SkipUnsupported) {
        if (parsed == nullptr || parsed->source == nullptr) {
            return FlatJsonFilterStatus::SkipUnsupported;
        }
    }

    if (parse_status == FlatJsonParseStatus::InvalidInput) {
        return FlatJsonFilterStatus::InvalidInput;
    }

    if (parse_status == FlatJsonParseStatus::Ok && parsed != nullptr) {
        if (context.matched_policy != nullptr &&
            context.matched_policy->field_filter.mode == policy::FieldFilterMode::Denylist &&
            !context.client_query_present) {
            return filter_flat_json_by_policy_denylist(*parsed, context,
                                                       context.matched_policy->field_filter, output,
                                                       output_capacity, output_length);
        }
    }

    FieldCountMetrics metrics{};
    FlatJsonFilterStatus status;
    if (context.matched_policy != nullptr &&
        context.matched_policy->field_filter.mode == policy::FieldFilterMode::Denylist &&
        !context.client_query_present) {
        status =
            filter_nested_json_by_policy_denylist(input_body, context.matched_policy->field_filter,
                                                  output, output_capacity, output_length, &metrics);
    } else {
        status = filter_nested_json_by_selected_fields(input_body, context, output, output_capacity,
                                                       output_length, &metrics);
    }

    if (status == FlatJsonFilterStatus::Ok || status == FlatJsonFilterStatus::OutputTooSmall) {
        assign_removed_field_count(context, metrics);
    }
    return status;
}

} // namespace bytetaper::json_transform

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_JSON_TRANSFORM_CONTENT_TYPE_H
#define BYTETAPER_JSON_TRANSFORM_CONTENT_TYPE_H

#include "apg/context.h"
#include "policy/field_filter_policy.h"

#include <cstddef>

namespace bytetaper::json_transform {

enum class JsonResponseKind {
    EligibleJson,
    SkipUnsupported,
    InvalidInput,
};

// Detects whether a Content-Type value should be treated as JSON.
// - Returns false only for invalid API usage (for example null out_kind).
// - Null/empty/unsupported content type returns true with SkipUnsupported.
bool detect_application_json_response(const char* content_type, JsonResponseKind* out_kind);

enum class FlatJsonParseStatus {
    Ok,
    SkipUnsupported,
    InvalidInput,
    InvalidJson,
};

struct FlatJsonFieldView {
    char key[policy::kMaxFieldNameLen] = {};
    std::size_t key_length = 0;
    std::size_t value_begin = 0;
    std::size_t value_end = 0;
};

struct ParsedFlatJsonObject {
    const char* source = nullptr;
    std::size_t source_length = 0;
    FlatJsonFieldView fields[policy::kMaxFields] = {};
    std::size_t field_count = 0;
};

FlatJsonParseStatus parse_flat_json_object(const char* body, JsonResponseKind response_kind,
                                           ParsedFlatJsonObject* out_object);

enum class FlatJsonFilterStatus {
    Ok,
    SkipUnsupported,
    InvalidInput,
    OutputTooSmall,
    InvalidJsonSafeError,
    Timeout,
};

// The filter copies value slices directly from parsed.source without coercion,
// preserving primitive JSON token representation (number/bool/null/string).
FlatJsonFilterStatus filter_flat_json_by_selected_fields(const ParsedFlatJsonObject& parsed,
                                                         const apg::ApgTransformContext& context,
                                                         char* output, std::size_t output_capacity,
                                                         std::size_t* output_length);

FlatJsonFilterStatus
filter_flat_json_by_policy_denylist(const ParsedFlatJsonObject& parsed,
                                    const apg::ApgTransformContext& context,
                                    const policy::FieldFilterPolicy& policy_filter, char* output,
                                    std::size_t output_capacity, std::size_t* output_length);

// Applies JSON filtering when enabled, otherwise returns the original body unchanged.
// Updates context.removed_field_count with the per-call removed key metric.
FlatJsonFilterStatus transform_flat_json_with_filter_toggle(const char* input_body,
                                                            const ParsedFlatJsonObject* parsed,
                                                            const apg::ApgTransformContext& context,
                                                            bool filtering_enabled, char* output,
                                                            std::size_t output_capacity,
                                                            std::size_t* output_length);

FlatJsonFilterStatus transform_flat_json_with_filter_toggle(
    const char* input_body, FlatJsonParseStatus parse_status, const ParsedFlatJsonObject* parsed,
    const apg::ApgTransformContext& context, bool filtering_enabled, char* output,
    std::size_t output_capacity, std::size_t* output_length);

} // namespace bytetaper::json_transform

#endif // BYTETAPER_JSON_TRANSFORM_CONTENT_TYPE_H

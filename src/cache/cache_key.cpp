// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"

#include <cstdio>
#include <cstring>

namespace bytetaper::cache {

namespace {

bool key_append(char** pos, std::size_t* remaining, const char* s, std::size_t len) {
    if (len > *remaining) {
        return false;
    }
    std::memcpy(*pos, s, len);
    *pos += len;
    *remaining -= len;
    return true;
}

bool key_append_char(char** pos, std::size_t* remaining, char c) {
    if (*remaining == 0) {
        return false;
    }
    **pos = c;
    ++(*pos);
    --(*remaining);
    return true;
}

} // namespace

bool build_cache_key(const CacheKeyInput& input, char* out_buf, std::size_t out_size) {
    if (input.method != policy::HttpMethod::Get) {
        return false;
    }
    if (input.route_id == nullptr || input.path == nullptr) {
        return false;
    }
    if (out_buf == nullptr || out_size == 0) {
        return false;
    }

    // Copy and sort/dedup selected_fields pointers (max 16 elements).
    const std::size_t input_count = input.selected_field_count < policy::kMaxFields
                                        ? input.selected_field_count
                                        : policy::kMaxFields;
    const char* temp_sorted[policy::kMaxFields];
    for (std::size_t i = 0; i < input_count; ++i) {
        temp_sorted[i] = (input.selected_fields != nullptr) ? input.selected_fields[i] : "";
    }

    // First sort them alphabetically
    for (std::size_t i = 0; i < input_count; ++i) {
        std::size_t min_idx = i;
        for (std::size_t j = i + 1; j < input_count; ++j) {
            if (std::strncmp(temp_sorted[j], temp_sorted[min_idx], policy::kMaxFieldNameLen) < 0) {
                min_idx = j;
            }
        }
        if (min_idx != i) {
            const char* tmp = temp_sorted[i];
            temp_sorted[i] = temp_sorted[min_idx];
            temp_sorted[min_idx] = tmp;
        }
    }

    // Now deduplicate adjacent duplicates
    const char* sorted[policy::kMaxFields];
    std::size_t count = 0;
    for (std::size_t i = 0; i < input_count; ++i) {
        if (i == 0 ||
            std::strncmp(temp_sorted[i], temp_sorted[i - 1], policy::kMaxFieldNameLen) != 0) {
            sorted[count++] = temp_sorted[i];
        }
    }

    char* pos = out_buf;
    std::size_t remaining = out_size - 1;

    // Emit "var:" prefix for variant keys
    if (input.variant) {
        if (!key_append(&pos, &remaining, "var:", 4)) {
            return false;
        }
    }

    // Format: GET|{route_id}|{path}|{query}|{f1},{f2}|{policy_version}
    if (!key_append(&pos, &remaining, "GET|", 4)) {
        return false;
    }
    const std::size_t route_id_len = std::strlen(input.route_id);
    if (!key_append(&pos, &remaining, input.route_id, route_id_len)) {
        return false;
    }
    if (!key_append_char(&pos, &remaining, '|')) {
        return false;
    }

    if (input.route_cache_epoch_ready) {
        char epoch_buf[32];
        int n = std::snprintf(epoch_buf, sizeof(epoch_buf), "epoch:%llu|",
                              static_cast<unsigned long long>(input.route_cache_epoch));
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(epoch_buf)) {
            return false;
        }
        if (!key_append(&pos, &remaining, epoch_buf, static_cast<std::size_t>(n))) {
            return false;
        }
    }

    const std::size_t path_len = std::strlen(input.path);
    if (!key_append(&pos, &remaining, input.path, path_len)) {
        return false;
    }
    if (!key_append_char(&pos, &remaining, '|')) {
        return false;
    }
    if (input.query != nullptr && input.query[0] != '\0') {
        const std::size_t query_len = std::strlen(input.query);
        if (!key_append(&pos, &remaining, input.query, query_len)) {
            return false;
        }
    }
    if (!key_append_char(&pos, &remaining, '|')) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            if (!key_append_char(&pos, &remaining, ',')) {
                return false;
            }
        }
        const std::size_t flen = std::strlen(sorted[i]);
        if (!key_append(&pos, &remaining, sorted[i], flen)) {
            return false;
        }
    }
    if (!key_append_char(&pos, &remaining, '|')) {
        return false;
    }
    if (input.policy_version != nullptr && input.policy_version[0] != '\0') {
        const std::size_t ver_len = std::strlen(input.policy_version);
        if (!key_append(&pos, &remaining, input.policy_version, ver_len)) {
            return false;
        }
    }

    if (input.vary_header_count > 0) {
        if (!key_append(&pos, &remaining, "|vary:", 6)) {
            return false;
        }
        for (std::size_t i = 0; i < input.vary_header_count; ++i) {
            if (i > 0 && !key_append_char(&pos, &remaining, ',')) {
                return false;
            }
            if (input.vary_headers[i].name == nullptr ||
                input.vary_headers[i].value_hash == nullptr) {
                return false;
            }
            const std::size_t nlen = std::strlen(input.vary_headers[i].name);
            if (!key_append(&pos, &remaining, input.vary_headers[i].name, nlen)) {
                return false;
            }
            if (!key_append_char(&pos, &remaining, '=')) {
                return false;
            }
            const std::size_t vlen = std::strlen(input.vary_headers[i].value_hash);
            if (!key_append(&pos, &remaining, input.vary_headers[i].value_hash, vlen)) {
                return false;
            }
        }
    }

    if (input.private_cache) {
        if (input.auth_scope == nullptr || input.auth_scope[0] == '\0') {
            return false; // missing scope rejects private cache
        }
        // append |scope:{auth_scope}
        int n = std::snprintf(pos, remaining, "|scope:%s", input.auth_scope);
        if (n < 0 || static_cast<std::size_t>(n) >= remaining) {
            return false;
        }
        pos += n;
        remaining -= static_cast<std::size_t>(n);
    }

    *pos = '\0';
    return true;
}

std::size_t sanitize_query_strip_fields_param(const char* query, char* out_buf,
                                              std::size_t out_size) {
    if (query == nullptr || out_buf == nullptr || out_size == 0) {
        if (out_buf != nullptr && out_size > 0) {
            out_buf[0] = '\0';
        }
        return 0;
    }

    const char* p = query;
    bool has_leading_q = (*p == '?');
    if (has_leading_q) {
        p++;
    }

    char* dest = out_buf;
    std::size_t dest_remaining = out_size - 1; // reserve for null terminator

    char* dest_start = dest;
    if (has_leading_q) {
        dest++;
        dest_remaining--;
    }

    bool first = true;
    while (*p != '\0') {
        const char* param_start = p;
        while (*p != '\0' && *p != '&') {
            p++;
        }
        std::size_t param_len = p - param_start;

        bool is_fields = false;
        if (param_len >= 7 && std::strncmp(param_start, "fields=", 7) == 0) {
            is_fields = true;
        }

        if (!is_fields && param_len > 0) {
            std::size_t needed = param_len;
            if (!first) {
                needed += 1; // for '&'
            }
            if (needed > dest_remaining) {
                out_buf[0] = '\0';
                return 0; // overflow
            }
            if (!first) {
                *dest++ = '&';
                dest_remaining--;
            }
            std::memcpy(dest, param_start, param_len);
            dest += param_len;
            dest_remaining -= param_len;
            first = false;
        }

        if (*p == '&') {
            p++;
        }
    }

    if (dest == (has_leading_q ? dest_start + 1 : dest_start)) {
        dest_start[0] = '\0';
        return 0;
    }

    if (has_leading_q) {
        dest_start[0] = '?';
    }

    *dest = '\0';
    return dest - dest_start;
}

} // namespace bytetaper::cache

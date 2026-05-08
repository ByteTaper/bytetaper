// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/header_view.h"

#include "envoy/config/core/v3/base.pb.h"

#include <string>

namespace bytetaper::extproc {

RequestHeaderView scan_request_headers(const envoy::config::core::v3::HeaderMap& headers) {
    RequestHeaderView view{};
    for (const auto& header : headers.headers()) {
        const std::string& key = header.key();
        const std::string& val = header.raw_value().empty() ? header.value() : header.raw_value();

        if (key == ":path") {
            view.path = val.c_str();
            view.path_len = val.size();
        } else if (key == ":method") {
            view.method = val.c_str();
            view.method_len = val.size();
        } else if (key == "accept-encoding") {
            view.accept_encoding = val.c_str();
            view.accept_encoding_len = val.size();
        } else if (key == "authorization") {
            view.authorization = val.c_str();
            view.authorization_len = val.size();
        } else if (key == "cookie") {
            view.cookie = val.c_str();
            view.cookie_len = val.size();
        }
    }
    return view;
}

ResponseHeaderView scan_response_headers(const envoy::config::core::v3::HeaderMap& headers) {
    ResponseHeaderView view{};
    for (const auto& header : headers.headers()) {
        const std::string& key = header.key();
        const std::string& val = header.raw_value().empty() ? header.value() : header.raw_value();

        if (key == ":status") {
            view.status = val.c_str();
            view.status_len = val.size();
        } else if (key == "content-type") {
            view.content_type = val.c_str();
            view.content_type_len = val.size();
        } else if (key == "content-encoding") {
            view.content_encoding = val.c_str();
            view.content_encoding_len = val.size();
        } else if (key == "content-length") {
            view.content_length = val.c_str();
            view.content_length_len = val.size();
        }
    }
    return view;
}

static bool ascii_case_insensitive_equal(const std::string& a, const char* b) {
    if (b == nullptr)
        return false;
    std::size_t len_a = a.size();
    for (std::size_t i = 0; i < len_a; ++i) {
        if (b[i] == '\0')
            return false;
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return false;
    }
    return b[len_a] == '\0';
}

bool read_header_value_case_insensitive(const envoy::config::core::v3::HeaderMap& headers,
                                        const char* header_name, const char** value_out,
                                        std::size_t* value_len_out) {
    if (header_name == nullptr || value_out == nullptr || value_len_out == nullptr) {
        return false;
    }
    for (const auto& header : headers.headers()) {
        if (ascii_case_insensitive_equal(header.key(), header_name)) {
            const std::string& val =
                header.raw_value().empty() ? header.value() : header.raw_value();
            *value_out = val.c_str();
            *value_len_out = val.size();
            return true;
        }
    }
    return false;
}

} // namespace bytetaper::extproc

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_EXTPROC_HEADER_VIEW_H
#define BYTETAPER_EXTPROC_HEADER_VIEW_H

#include <cstddef>

namespace bytetaper::extproc {

// Non-owning view of interesting request headers.
// Pointers are valid for the lifetime of the ProcessingRequest headers map.
struct RequestHeaderView {
    const char* path = nullptr;
    std::size_t path_len = 0;
    const char* method = nullptr;
    std::size_t method_len = 0;
    const char* accept_encoding = nullptr;
    std::size_t accept_encoding_len = 0;
    const char* authorization = nullptr;
    std::size_t authorization_len = 0;
    const char* cookie = nullptr;
    std::size_t cookie_len = 0;
};

// Non-owning view of interesting response headers.
struct ResponseHeaderView {
    const char* status = nullptr;
    std::size_t status_len = 0;
    const char* content_type = nullptr;
    std::size_t content_type_len = 0;
    const char* content_encoding = nullptr;
    std::size_t content_encoding_len = 0;
    const char* content_length = nullptr;
    std::size_t content_length_len = 0;
};

} // namespace bytetaper::extproc

namespace envoy::config::core::v3 {
class HeaderMap;
}

namespace bytetaper::extproc {

RequestHeaderView scan_request_headers(const envoy::config::core::v3::HeaderMap& headers);
ResponseHeaderView scan_response_headers(const envoy::config::core::v3::HeaderMap& headers);

bool read_header_value_case_insensitive(const envoy::config::core::v3::HeaderMap& headers,
                                        const char* header_name, const char** value_out,
                                        std::size_t* value_len_out);

} // namespace bytetaper::extproc

#endif // BYTETAPER_EXTPROC_HEADER_VIEW_H

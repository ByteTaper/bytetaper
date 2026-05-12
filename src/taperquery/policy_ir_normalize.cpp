// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_normalize.h"

#include <algorithm>
#include <cctype>

namespace bytetaper::taperquery {

namespace {

std::string to_lowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::vector<std::string> stable_dedup_and_lowercase(const std::vector<std::string>& input) {
    std::vector<std::string> result;
    for (const auto& item : input) {
        std::string lowered = to_lowercase(item);
        if (std::find(result.begin(), result.end(), lowered) == result.end()) {
            result.push_back(lowered);
        }
    }
    return result;
}

} // namespace

TqRoutePolicy normalize_route_policy_ir(const TqRoutePolicy& input) {
    TqRoutePolicy result = input;
    // Lowercase auth_scope_header
    result.cache.private_cache.auth_scope_header =
        to_lowercase(result.cache.private_cache.auth_scope_header);
    // Lowercase and stable-dedup vary_headers names
    result.cache.vary_headers.names = stable_dedup_and_lowercase(result.cache.vary_headers.names);
    return result;
}

TqPolicyDocument normalize_policy_ir(const TqPolicyDocument& input) {
    TqPolicyDocument result = input;
    result.routes.clear();
    for (const auto& route : input.routes) {
        result.routes.push_back(normalize_route_policy_ir(route));
    }
    return result;
}

} // namespace bytetaper::taperquery

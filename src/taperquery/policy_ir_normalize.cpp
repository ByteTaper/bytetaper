// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_normalize.h"

#include "taperquery/policy_ir_version.h"

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

    // Normalize field_variant fields to 0 if not enabled to ensure round-trip identity parity
    if (!result.cache.field_variant.enabled) {
        result.cache.field_variant.max_variants_per_route = 0;
        result.cache.field_variant.min_field_count = 0;
        result.cache.field_variant.max_field_count = 0;
        result.cache.field_variant.admission_threshold = 0;
        result.cache.field_variant.ttl_max_ms = 0;
    }

    return result;
}

TqPolicyDocument normalize_policy_ir(const TqPolicyDocument& input) {
    TqPolicyDocument result = input;

    if (result.version.policy_ir_version.empty()) {
        result.version.policy_ir_version = kCurrentPolicyIrVersion;
    }
    if (result.version.identity_version.empty()) {
        result.version.identity_version = kCurrentPolicyIdentityVersion;
    }

    std::string resolved_schema;
    if (!result.version.source_schema_version.empty()) {
        resolved_schema = result.version.source_schema_version;
    } else if (!result.schema_version.empty()) {
        resolved_schema = result.schema_version;
    } else if (result.source_name.rfind(".yaml") != std::string::npos ||
               result.source_name.rfind(".yml") != std::string::npos) {
        resolved_schema = "yaml/v1";
    } else {
        resolved_schema = "tq/v1";
    }
    result.version.source_schema_version = resolved_schema;
    result.schema_version = resolved_schema;

    result.routes.clear();
    for (const auto& route : input.routes) {
        result.routes.push_back(normalize_route_policy_ir(route));
    }
    return result;
}

} // namespace bytetaper::taperquery

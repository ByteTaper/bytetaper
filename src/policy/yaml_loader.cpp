// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/yaml_loader.h"

#include "policy/policy_identity.h"

#include <cctype>
#include <cstring>
#include <yaml-cpp/yaml.h>

namespace bytetaper::policy {

namespace {

static bool parse_cache_layers(const YAML::Node& cache_node, RoutePolicy& policy,
                               PolicyFileResult* result) {
    if (!cache_node["layers"]) {
        return true; // absent is fine (no change)
    }
    const YAML::Node& layers_node = cache_node["layers"];
    if (!layers_node.IsMap()) {
        result->error = "cache.layers must be a map";
        return false;
    }

    if (layers_node["l1"]) {
        const YAML::Node& l1_node = layers_node["l1"];
        if (!l1_node.IsMap()) {
            result->error = "cache.layers.l1 must be a map";
            return false;
        }
        if (l1_node["enabled"]) {
            if (!l1_node["enabled"].IsScalar()) {
                result->error = "cache.layers.l1.enabled must be a scalar boolean";
                return false;
            }
            policy.cache.l1.enabled = l1_node["enabled"].as<bool>();
        }
        if (l1_node["capacity_entries"]) {
            if (!l1_node["capacity_entries"].IsScalar()) {
                result->error = "cache.layers.l1.capacity_entries must be a scalar integer";
                return false;
            }
            try {
                policy.cache.l1.capacity_entries = l1_node["capacity_entries"].as<std::uint32_t>();
            } catch (...) {
                result->error = "cache.layers.l1.capacity_entries must be a valid positive integer";
                return false;
            }
        }
    }

    if (layers_node["l2"]) {
        const YAML::Node& l2_node = layers_node["l2"];
        if (!l2_node.IsMap()) {
            result->error = "cache.layers.l2 must be a map";
            return false;
        }
        if (l2_node["enabled"]) {
            if (!l2_node["enabled"].IsScalar()) {
                result->error = "cache.layers.l2.enabled must be a scalar boolean";
                return false;
            }
            policy.cache.l2.enabled = l2_node["enabled"].as<bool>();
        }
        if (l2_node["path"]) {
            if (!l2_node["path"].IsScalar()) {
                result->error = "cache.layers.l2.path must be a scalar string";
                return false;
            }
            const std::string path = l2_node["path"].as<std::string>();
            if (path.empty()) {
                result->error = "cache.layers.l2.path must not be empty";
                return false;
            }
            if (path.size() >= kMaxCachePathLen) {
                result->error = "cache.layers.l2.path exceeds max length";
                return false;
            }
            std::strncpy(policy.cache.l2.path, path.c_str(), sizeof(policy.cache.l2.path) - 1);
            policy.cache.l2.path[sizeof(policy.cache.l2.path) - 1] = '\0';
        }
    }

    return true;
}

// Parse a single route node into a PolicyFileResult slot. Returns false on error.
bool parse_one_route(const YAML::Node& node, PolicyFileResult* result, std::size_t index) {
    if (!node["id"] || !node["id"].IsScalar()) {
        result->error = "route missing required 'id' field";
        return false;
    }
    if (!node["match"] || !node["match"]["kind"] || !node["match"]["prefix"]) {
        result->error = "route missing required 'match.kind' or 'match.prefix'";
        return false;
    }

    const std::string id = node["id"].as<std::string>();
    const std::string kind = node["match"]["kind"].as<std::string>();
    const std::string prefix = node["match"]["prefix"].as<std::string>();
    const std::string mut = node["mutation"] ? node["mutation"].as<std::string>() : "disabled";
    const std::string method = node["method"] ? node["method"].as<std::string>() : "any";

    if (id.size() >= kMaxRouteIdLen) {
        result->error = "route_id exceeds max length";
        return false;
    }
    if (prefix.size() >= kMaxPrefixLen) {
        result->error = "match_prefix exceeds max length";
        return false;
    }

    std::memcpy(result->route_id_storage[index], id.c_str(), id.size() + 1);
    std::memcpy(result->match_prefix_storage[index], prefix.c_str(), prefix.size() + 1);

    RoutePolicy& policy = result->policies[index];
    policy.route_id = result->route_id_storage[index];
    policy.match_prefix = result->match_prefix_storage[index];

    if (kind == "prefix") {
        policy.match_kind = RouteMatchKind::Prefix;
    } else if (kind == "exact") {
        policy.match_kind = RouteMatchKind::Exact;
    } else {
        result->error = "unknown match.kind (expected 'prefix' or 'exact')";
        return false;
    }

    if (mut == "disabled") {
        policy.mutation = MutationMode::Disabled;
    } else if (mut == "headers_only") {
        policy.mutation = MutationMode::HeadersOnly;
    } else if (mut == "full") {
        policy.mutation = MutationMode::Full;
    } else {
        result->error = "unknown mutation (expected 'disabled', 'headers_only', or 'full')";
        return false;
    }

    if (method == "any") {
        policy.allowed_method = HttpMethod::Any;
    } else if (method == "get") {
        policy.allowed_method = HttpMethod::Get;
    } else if (method == "post") {
        policy.allowed_method = HttpMethod::Post;
    } else if (method == "put") {
        policy.allowed_method = HttpMethod::Put;
    } else if (method == "delete") {
        policy.allowed_method = HttpMethod::Delete;
    } else if (method == "patch") {
        policy.allowed_method = HttpMethod::Patch;
    } else {
        result->error =
            "unknown method (expected 'get', 'post', 'put', 'delete', 'patch', or 'any')";
        return false;
    }

    if (node["field_filter"]) {
        const YAML::Node& filter_node = node["field_filter"];
        if (!filter_node["mode"] || !filter_node["mode"].IsScalar()) {
            result->error = "field_filter missing 'mode'";
            return false;
        }

        const std::string mode_str = filter_node["mode"].as<std::string>();
        if (mode_str == "none") {
            policy.field_filter.mode = FieldFilterMode::None;
        } else if (mode_str == "allowlist") {
            policy.field_filter.mode = FieldFilterMode::Allowlist;
        } else if (mode_str == "denylist") {
            policy.field_filter.mode = FieldFilterMode::Denylist;
        } else {
            result->error =
                "unknown field_filter.mode (expected 'none', 'allowlist', or 'denylist')";
            return false;
        }

        if (policy.field_filter.mode != FieldFilterMode::None) {
            if (!filter_node["fields"] || !filter_node["fields"].IsSequence()) {
                result->error = "field_filter with mode must have 'fields' sequence";
                return false;
            }

            const YAML::Node& fields_node = filter_node["fields"];
            if (fields_node.size() > kMaxFields) {
                result->error = "too many field_filter fields (exceeds kMaxFields)";
                return false;
            }

            for (std::size_t i = 0; i < fields_node.size(); ++i) {
                if (!fields_node[i].IsScalar()) {
                    result->error = "field_filter field must be a scalar string";
                    return false;
                }
                const std::string f = fields_node[i].as<std::string>();
                if (f.empty()) {
                    result->error = "field_filter field name cannot be empty";
                    return false;
                }
                if (f.size() >= kMaxFieldNameLen) {
                    result->error = "field_filter field name exceeds max length";
                    return false;
                }
                std::memcpy(policy.field_filter.fields[i], f.c_str(), f.size() + 1);
            }
            policy.field_filter.field_count = fields_node.size();
        }
    }

    if (node["max_response_bytes"]) {
        if (!node["max_response_bytes"].IsScalar()) {
            result->error = "max_response_bytes must be a scalar integer";
            return false;
        }
        try {
            const std::uint64_t val = node["max_response_bytes"].as<std::uint64_t>();
            if (val > (std::uint64_t) 0xFFFFFFFFu) {
                result->error = "max_response_bytes exceeds uint32 max";
                return false;
            }
            policy.max_response_bytes = static_cast<std::uint32_t>(val);
        } catch (...) {
            result->error = "max_response_bytes must be a valid positive integer";
            return false;
        }
    }

    if (node["cache"]) {
        const YAML::Node& cache_node = node["cache"];
        if (cache_node["behavior"]) {
            if (!cache_node["behavior"].IsScalar()) {
                result->error = "cache.behavior must be a scalar string";
                return false;
            }
            const std::string behavior_str = cache_node["behavior"].as<std::string>();
            if (behavior_str == "default") {
                policy.cache.behavior = CacheBehavior::Default;
            } else if (behavior_str == "bypass") {
                policy.cache.behavior = CacheBehavior::Bypass;
            } else if (behavior_str == "store") {
                policy.cache.behavior = CacheBehavior::Store;
            } else {
                result->error = "unknown cache.behavior (expected 'default', 'bypass', or 'store')";
                return false;
            }
        }

        if (cache_node["ttl_seconds"]) {
            if (!cache_node["ttl_seconds"].IsScalar()) {
                result->error = "cache.ttl_seconds must be a scalar integer";
                return false;
            }
            try {
                policy.cache.ttl_seconds = cache_node["ttl_seconds"].as<std::uint32_t>();
            } catch (...) {
                result->error = "cache.ttl_seconds must be a valid positive integer";
                return false;
            }
        }

        if (cache_node["enabled"]) {
            policy.cache.enabled = cache_node["enabled"].as<bool>();
        }

        if (!parse_cache_layers(cache_node, policy, result)) {
            return false;
        }

        if (cache_node["private_cache"]) {
            if (!cache_node["private_cache"].IsScalar()) {
                result->error = "cache.private_cache must be a scalar boolean";
                return false;
            }
            policy.cache.private_cache = cache_node["private_cache"].as<bool>();
        }

        if (cache_node["auth_scope_header"]) {
            if (!cache_node["auth_scope_header"].IsScalar()) {
                result->error = "cache.auth_scope_header must be a scalar string";
                return false;
            }
            const std::string hdr = cache_node["auth_scope_header"].as<std::string>();
            if (hdr.empty()) {
                result->error = "cache.auth_scope_header must not be empty";
                return false;
            }
            if (hdr.size() >= sizeof(policy.cache.auth_scope_header)) {
                result->error = "cache.auth_scope_header exceeds max length";
                return false;
            }
            // lowercase: consistent with cache vary_header normalization already in this file
            std::string lc = hdr;
            for (char& c : lc)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::strncpy(policy.cache.auth_scope_header, lc.c_str(),
                         sizeof(policy.cache.auth_scope_header) - 1);
            policy.cache.auth_scope_header[sizeof(policy.cache.auth_scope_header) - 1] = '\0';
        }

        if (cache_node["field_variant"]) {
            const YAML::Node& fv_node = cache_node["field_variant"];
            if (fv_node["enabled"]) {
                policy.cache.field_variant.enabled = fv_node["enabled"].as<bool>();
            }
            if (fv_node["max_variants_per_route"]) {
                policy.cache.field_variant.max_variants_per_route =
                    fv_node["max_variants_per_route"].as<std::uint32_t>();
            }
            if (fv_node["min_field_count"]) {
                policy.cache.field_variant.min_field_count =
                    fv_node["min_field_count"].as<std::uint32_t>();
            }
            if (fv_node["max_field_count"]) {
                policy.cache.field_variant.max_field_count =
                    fv_node["max_field_count"].as<std::uint32_t>();
            }
            if (fv_node["admission_threshold"]) {
                policy.cache.field_variant.admission_threshold =
                    fv_node["admission_threshold"].as<std::uint32_t>();
            }
            if (fv_node["ttl_max_ms"]) {
                policy.cache.field_variant.ttl_max_ms = fv_node["ttl_max_ms"].as<std::uint32_t>();
            }
        }

        if (cache_node["vary_headers"]) {
            const YAML::Node& vh = cache_node["vary_headers"];
            if (!vh.IsSequence()) {
                result->error = "cache.vary_headers must be a list";
                return false;
            }
            if (vh.size() > policy::kMaxCacheVaryHeaders) {
                result->error = "too many cache.vary_headers (max 8)";
                return false;
            }
            std::size_t vh_count = 0;
            for (const auto& item : vh) {
                if (!item.IsScalar()) {
                    result->error = "cache.vary_headers entries must be strings";
                    return false;
                }
                const std::string raw = item.as<std::string>();
                if (raw.empty()) {
                    result->error = "cache.vary_headers entry must not be empty";
                    return false;
                }
                if (raw.size() >= policy::kMaxCacheVaryHeaderNameLen) {
                    result->error = "cache.vary_headers entry name too long";
                    return false;
                }
                // Normalize to lowercase
                char lc[policy::kMaxCacheVaryHeaderNameLen] = {};
                for (std::size_t i = 0; i < raw.size(); ++i) {
                    lc[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i])));
                }
                // Deduplicate: skip if already present
                bool dup = false;
                for (std::size_t j = 0; j < vh_count; ++j) {
                    if (std::strncmp(policy.cache.vary_headers.names[j], lc,
                                     policy::kMaxCacheVaryHeaderNameLen) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    std::strncpy(policy.cache.vary_headers.names[vh_count++], lc,
                                 policy::kMaxCacheVaryHeaderNameLen - 1);
                }
            }
            policy.cache.vary_headers.count = vh_count;
        }

        if (cache_node["invalidation"]) {
            const YAML::Node& inv_node = cache_node["invalidation"];
            if (inv_node["enabled"]) {
                policy.cache.invalidation.enabled = inv_node["enabled"].as<bool>();
            }
            if (inv_node["on_methods"]) {
                if (!inv_node["on_methods"].IsSequence()) {
                    result->error = "cache.invalidation on_methods must be a sequence";
                    return false;
                }
                for (const auto& m_node : inv_node["on_methods"]) {
                    std::string m = m_node.as<std::string>();
                    for (char& c : m)
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    if (m == "PATCH") {
                        policy.cache.invalidation.on_patch = true;
                    } else if (m == "PUT") {
                        policy.cache.invalidation.on_put = true;
                    } else if (m == "DELETE") {
                        policy.cache.invalidation.on_delete = true;
                    } else if (m == "GET") {
                        result->error = "cache.invalidation on_methods cannot contain GET";
                        return false;
                    } else if (m == "POST") {
                        result->error = "cache.invalidation on_methods cannot contain POST";
                        return false;
                    } else {
                        result->error = "unknown method in cache.invalidation on_methods";
                        return false;
                    }
                }
            }
            if (inv_node["timing"]) {
                const std::string t = inv_node["timing"].as<std::string>();
                if (t == "after_successful_upstream_response") {
                    policy.cache.invalidation.timing =
                        CacheInvalidationTiming::AfterSuccessfulUpstreamResponse;
                } else {
                    result->error = "unknown cache.invalidation timing";
                    return false;
                }
            }
            if (inv_node["success_status"]) {
                const YAML::Node& ss_node = inv_node["success_status"];
                if (ss_node["min"]) {
                    policy.cache.invalidation.success_status_min =
                        ss_node["min"].as<std::uint16_t>();
                }
                if (ss_node["max"]) {
                    policy.cache.invalidation.success_status_max =
                        ss_node["max"].as<std::uint16_t>();
                }
                if (policy.cache.invalidation.success_status_min < 100) {
                    result->error = "cache.invalidation success_status min must be >= 100";
                    return false;
                }
                if (policy.cache.invalidation.success_status_max > 599) {
                    result->error = "cache.invalidation success_status max must be <= 599";
                    return false;
                }
                if (policy.cache.invalidation.success_status_min >
                    policy.cache.invalidation.success_status_max) {
                    result->error =
                        "cache.invalidation success_status min cannot be greater than max";
                    return false;
                }
            }
            if (inv_node["targets"]) {
                const YAML::Node& targets_node = inv_node["targets"];
                if (!targets_node.IsSequence()) {
                    result->error = "cache.invalidation targets must be a sequence";
                    return false;
                }
                if (targets_node.size() == 0) {
                    result->error = "cache.invalidation targets sequence cannot be empty";
                    return false;
                }
                if (targets_node.size() > kMaxCacheInvalidationTargets) {
                    result->error = "cache.invalidation targets exceed maximum allowed";
                    return false;
                }
                for (std::size_t i = 0; i < targets_node.size(); ++i) {
                    const YAML::Node& target = targets_node[i];
                    if (!target["route_id"] || !target["route_id"].IsScalar()) {
                        result->error = "cache.invalidation target missing route_id";
                        return false;
                    }
                    const std::string rid = target["route_id"].as<std::string>();
                    if (rid.empty() ||
                        rid.size() >= sizeof(CacheInvalidationTargetPolicy::route_id)) {
                        result->error = "cache.invalidation target route_id invalid or too long";
                        return false;
                    }
                    std::strncpy(policy.cache.invalidation.targets[i].route_id, rid.c_str(),
                                 sizeof(CacheInvalidationTargetPolicy::route_id) - 1);
                    policy.cache.invalidation.targets[i]
                        .route_id[sizeof(CacheInvalidationTargetPolicy::route_id) - 1] = '\0';

                    if (target["strategy"]) {
                        const std::string st = target["strategy"].as<std::string>();
                        if (st == "route_epoch") {
                            policy.cache.invalidation.targets[i].strategy =
                                CacheInvalidationStrategy::RouteEpoch;
                        } else if (st == "exact_key") {
                            policy.cache.invalidation.targets[i].strategy =
                                CacheInvalidationStrategy::ExactKey;
                        } else if (st == "prefix") {
                            policy.cache.invalidation.targets[i].strategy =
                                CacheInvalidationStrategy::Prefix;
                        } else {
                            result->error = "unknown cache.invalidation strategy";
                            return false;
                        }
                    } else {
                        policy.cache.invalidation.targets[i].strategy =
                            CacheInvalidationStrategy::RouteEpoch;
                    }
                }
                policy.cache.invalidation.target_count = targets_node.size();
            } else if (policy.cache.invalidation.enabled) {
                result->error = "cache.invalidation enabled requires targets";
                return false;
            }
        }
    }

    if (node["failure_mode"]) {
        if (!node["failure_mode"].IsScalar()) {
            result->error = "failure_mode must be a scalar string";
            return false;
        }
        const std::string fail_mode_str = node["failure_mode"].as<std::string>();
        if (fail_mode_str == "fail_open") {
            policy.failure_mode = FailureMode::FailOpen;
        } else if (fail_mode_str == "fail_closed") {
            policy.failure_mode = FailureMode::FailClosed;
        } else {
            result->error = "unknown failure_mode (expected 'fail_open' or 'fail_closed')";
            return false;
        }
    }
    if (node["pagination"]) {
        const YAML::Node& pag_node = node["pagination"];
        policy.pagination.enabled = pag_node["enabled"] ? pag_node["enabled"].as<bool>() : false;
        if (policy.pagination.enabled) {
            if (pag_node["mode"]) {
                const std::string mode_str = pag_node["mode"].as<std::string>();
                if (mode_str == "limit_offset") {
                    policy.pagination.mode = PaginationMode::LimitOffset;
                } else if (mode_str == "cursor") {
                    policy.pagination.mode = PaginationMode::Cursor;
                } else {
                    result->error = "unknown pagination.mode (expected 'limit_offset' or 'cursor')";
                    return false;
                }
            }
            if (pag_node["limit_param"]) {
                const std::string p = pag_node["limit_param"].as<std::string>();
                if (p.size() >= 32) {
                    result->error = "pagination.limit_param too long";
                    return false;
                }
                std::strncpy(policy.pagination.limit_param, p.c_str(), 31);
                policy.pagination.limit_param[31] = '\0';
            }
            if (pag_node["offset_param"]) {
                const std::string p = pag_node["offset_param"].as<std::string>();
                if (p.size() >= 32) {
                    result->error = "pagination.offset_param too long";
                    return false;
                }
                std::strncpy(policy.pagination.offset_param, p.c_str(), 31);
                policy.pagination.offset_param[31] = '\0';
            }
            if (pag_node["default_limit"]) {
                policy.pagination.default_limit = pag_node["default_limit"].as<std::uint32_t>();
            }
            if (pag_node["max_limit"]) {
                policy.pagination.max_limit = pag_node["max_limit"].as<std::uint32_t>();
            }
            if (pag_node["upstream_supports_pagination"]) {
                policy.pagination.upstream_supports_pagination =
                    pag_node["upstream_supports_pagination"].as<bool>();
            }
            if (pag_node["max_response_bytes_warning"]) {
                policy.pagination.max_response_bytes_warning =
                    pag_node["max_response_bytes_warning"].as<std::uint32_t>();
            }
        }
    }

    if (node["compression"]) {
        const YAML::Node& comp_node = node["compression"];
        policy.compression.enabled = comp_node["enabled"] ? comp_node["enabled"].as<bool>() : false;
        if (policy.compression.enabled) {
            if (comp_node["min_size_bytes"]) {
                policy.compression.min_size_bytes = comp_node["min_size_bytes"].as<std::uint32_t>();
            }
            if (comp_node["eligible_content_types"]) {
                const YAML::Node& types_node = comp_node["eligible_content_types"];
                if (types_node.size() > kMaxEligibleContentTypes) {
                    result->error = "too many compression.eligible_content_types";
                    return false;
                }
                for (std::size_t i = 0; i < types_node.size(); ++i) {
                    const std::string t = types_node[i].as<std::string>();
                    if (t.size() >= kMaxContentTypeLen) {
                        result->error = "compression.eligible_content_types entry too long";
                        return false;
                    }
                    std::strncpy(policy.compression.eligible_content_types[i], t.c_str(),
                                 kMaxContentTypeLen - 1);
                    policy.compression.eligible_content_types[i][kMaxContentTypeLen - 1] = '\0';
                }
                policy.compression.eligible_content_type_count = types_node.size();
            }
            if (comp_node["preferred_algorithms"]) {
                const YAML::Node& algs_node = comp_node["preferred_algorithms"];
                if (algs_node.size() > kMaxCompressionAlgorithms) {
                    result->error = "too many compression.preferred_algorithms";
                    return false;
                }
                for (std::size_t i = 0; i < algs_node.size(); ++i) {
                    const std::string a = algs_node[i].as<std::string>();
                    if (a == "gzip") {
                        policy.compression.preferred_algorithms[i] = CompressionAlgorithm::Gzip;
                    } else if (a == "brotli") {
                        policy.compression.preferred_algorithms[i] = CompressionAlgorithm::Brotli;
                    } else if (a == "zstd") {
                        policy.compression.preferred_algorithms[i] = CompressionAlgorithm::Zstd;
                    } else {
                        result->error = "unknown compression algorithm";
                        return false;
                    }
                }
                policy.compression.preferred_algorithm_count = algs_node.size();
            }
            if (comp_node["already_encoded_behavior"]) {
                const std::string b = comp_node["already_encoded_behavior"].as<std::string>();
                if (b == "skip") {
                    policy.compression.already_encoded_behavior = AlreadyEncodedBehavior::Skip;
                } else if (b == "passthrough") {
                    policy.compression.already_encoded_behavior =
                        AlreadyEncodedBehavior::Passthrough;
                } else {
                    result->error = "unknown already_encoded_behavior";
                    return false;
                }
            }
        }
    }

    if (node["coalescing"]) {
        const YAML::Node& coal_node = node["coalescing"];
        policy.coalescing.enabled = coal_node["enabled"] ? coal_node["enabled"].as<bool>() : false;
        if (policy.coalescing.enabled) {
            if (coal_node["mode"]) {
                const std::string m = coal_node["mode"].as<std::string>();
                if (m == "cache_assisted") {
                    policy.coalescing.mode = CoalescingMode::CacheAssisted;
                } else {
                    result->error = "unknown coalescing mode (expected 'cache_assisted')";
                    return false;
                }
            }
            if (coal_node["backend_timeout_ms"]) {
                policy.coalescing.backend_timeout_ms =
                    coal_node["backend_timeout_ms"].as<std::uint32_t>();
            }
            if (coal_node["handoff_buffer_ms"]) {
                policy.coalescing.handoff_buffer_ms =
                    coal_node["handoff_buffer_ms"].as<std::uint32_t>();
            }
            if (coal_node["result_ready_retention_ms"]) {
                policy.coalescing.result_ready_retention_ms =
                    coal_node["result_ready_retention_ms"].as<std::uint32_t>();
            }
            if (coal_node["wait_window_ms"] && coal_node["backend_timeout_ms"]) {
                result->error =
                    "coalescing: 'wait_window_ms' and 'backend_timeout_ms' are both set; "
                    "remove 'wait_window_ms' — it is deprecated and ignored when "
                    "'backend_timeout_ms' is present";
                return false;
            }
            // backward compat: wait_window_ms maps to backend_timeout_ms
            if (coal_node["wait_window_ms"] && !coal_node["backend_timeout_ms"]) {
                policy.coalescing.backend_timeout_ms =
                    coal_node["wait_window_ms"].as<std::uint32_t>();

                if (!coal_node["handoff_buffer_ms"]) {
                    policy.coalescing.handoff_buffer_ms =
                        std::max(1U, policy.coalescing.backend_timeout_ms / 2);
                }
                if (!coal_node["result_ready_retention_ms"]) {
                    policy.coalescing.result_ready_retention_ms =
                        std::max(10U, policy.coalescing.backend_timeout_ms / 4);
                    if (policy.coalescing.result_ready_retention_ms >=
                        policy.coalescing.backend_timeout_ms) {
                        policy.coalescing.result_ready_retention_ms =
                            policy.coalescing.backend_timeout_ms > 10
                                ? policy.coalescing.backend_timeout_ms - 1
                                : 10;
                    }
                }
                std::snprintf(result->warning, sizeof(result->warning),
                              "coalescing: 'wait_window_ms' is deprecated; replace with "
                              "'backend_timeout_ms: %u' and 'handoff_buffer_ms: %u'",
                              policy.coalescing.backend_timeout_ms,
                              policy.coalescing.handoff_buffer_ms);
            }
            if (coal_node["max_waiters_per_key"]) {
                policy.coalescing.max_waiters_per_key =
                    coal_node["max_waiters_per_key"].as<std::uint32_t>();
            }
            if (coal_node["require_cache_enabled"]) {
                policy.coalescing.require_cache_enabled =
                    coal_node["require_cache_enabled"].as<bool>();
            }
            if (coal_node["allow_authenticated"]) {
                policy.coalescing.allow_authenticated = coal_node["allow_authenticated"].as<bool>();
            }
            if (coal_node["max_follower_wait_budget_ms"]) {
                policy.coalescing.max_follower_wait_budget_ms =
                    coal_node["max_follower_wait_budget_ms"].as<std::uint32_t>();
            }
            if (coal_node["max_active_follower_waiters"]) {
                policy.coalescing.max_active_follower_waiters =
                    coal_node["max_active_follower_waiters"].as<std::uint32_t>();
            }
            if (coal_node["max_active_follower_waiters_per_shard"]) {
                policy.coalescing.max_active_follower_waiters_per_shard =
                    coal_node["max_active_follower_waiters_per_shard"].as<std::uint32_t>();
            }
        }
    }
    if (!copy_route_policy_identity_v2_to_legacy_slot(&policy)) {
        result->error = "failed to compute route policy identity";
        return false;
    }

    return true;
}

bool parse_document(const YAML::Node& doc, PolicyFileResult* result) {
    if (!doc["routes"] || !doc["routes"].IsSequence()) {
        result->error = "YAML document missing 'routes' sequence";
        return false;
    }

    const YAML::Node& routes = doc["routes"];
    if (routes.size() > kMaxRoutes) {
        result->error = "too many routes (exceeds kMaxRoutes)";
        return false;
    }

    for (std::size_t i = 0; i < routes.size(); ++i) {
        if (!parse_one_route(routes[i], result, i)) {
            return false;
        }
    }

    // Semantic checks and Cross-route invalidation target checks
    for (std::size_t i = 0; i < routes.size(); ++i) {
        const auto& r1 = result->policies[i];
        if (r1.cache.invalidation.enabled) {
            if (r1.allowed_method == HttpMethod::Get || r1.allowed_method == HttpMethod::Post ||
                r1.allowed_method == HttpMethod::Any) {
                result->error = "cache invalidation policy can only be defined on mutation routes "
                                "(PATCH, PUT, DELETE)";
                return false;
            }
            if (r1.cache.enabled && r1.cache.behavior == CacheBehavior::Store) {
                result->error = "route cannot declare both cache store and cache invalidation";
                return false;
            }

            for (std::size_t t = 0; t < r1.cache.invalidation.target_count; ++t) {
                const auto& target = r1.cache.invalidation.targets[t];
                if (target.route_id[0] == '\0')
                    continue;

                if (target.strategy == CacheInvalidationStrategy::Prefix) {
                    std::snprintf(
                        result->warning, sizeof(result->warning),
                        "route '%s' uses prefix invalidation strategy which may be expensive",
                        r1.route_id);
                }

                const RoutePolicy* target_route = nullptr;
                for (std::size_t j = 0; j < routes.size(); ++j) {
                    if (std::strcmp(result->policies[j].route_id, target.route_id) == 0) {
                        target_route = &result->policies[j];
                        break;
                    }
                }
                if (!target_route) {
                    result->error = "invalidation target route not found in document";
                    return false;
                }
                if (!target_route->cache.enabled ||
                    target_route->cache.behavior != CacheBehavior::Store) {
                    result->error =
                        "invalidation target route must have cache.enabled=true and behavior=store";
                    return false;
                }
                if (target_route->allowed_method != HttpMethod::Get) {
                    result->error = "invalidation target route must be a cacheable GET route";
                    return false;
                }
            }
        }
    }

    result->count = routes.size();
    result->ok = true;
    return true;
}

} // namespace

bool load_policy_from_string(const char* yaml_content, PolicyFileResult* result) {
    if (result == nullptr) {
        return false;
    }
    if (yaml_content == nullptr) {
        result->error = "yaml_content is null";
        return false;
    }
    try {
        const YAML::Node doc = YAML::Load(yaml_content);
        return parse_document(doc, result);
    } catch (const YAML::Exception& e) {
        result->error = "YAML parse error";
        return false;
    } catch (...) {
        result->error = "unknown error during YAML parse";
        return false;
    }
}

bool load_policy_from_file(const char* path, PolicyFileResult* result) {
    if (result == nullptr) {
        return false;
    }
    if (path == nullptr) {
        result->error = "path is null";
        return false;
    }
    try {
        const YAML::Node doc = YAML::LoadFile(path);
        return parse_document(doc, result);
    } catch (const YAML::Exception& e) {
        result->error = "YAML file load or parse error";
        return false;
    } catch (...) {
        result->error = "unknown error during YAML file load";
        return false;
    }
}

} // namespace bytetaper::policy

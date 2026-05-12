// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_validator.h"

#include "taperquery/policy_ir_version.h"
#include "taperquery/route_analysis.h"

#include <algorithm>
#include <set>

namespace bytetaper::taperquery {

namespace {

bool add_issue(TqPolicyValidationResult& result, TqPolicyValidationSeverity severity,
               TqPolicyValidationSubsystem subsystem, const std::string& route_id,
               const std::string& field_path, const std::string& code, const std::string& reason,
               const std::string& hint, const TqPolicyValidationOptions& options) {
    if (severity == TqPolicyValidationSeverity::Warning && !options.include_warnings) {
        return true;
    }
    if (severity == TqPolicyValidationSeverity::Error) {
        result.ok = false;
    }
    TqPolicyValidationIssue issue;
    issue.severity = severity;
    issue.subsystem = subsystem;
    issue.route_id = route_id;
    issue.field_path = field_path;
    issue.code = code;
    issue.reason = reason;
    issue.hint = hint;
    result.issues.push_back(issue);

    if (severity == TqPolicyValidationSeverity::Error && !options.collect_all) {
        return false;
    }
    return true;
}

bool is_valid_http_header_name(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            continue;
        }
        if (c == '-' || c == '_' || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
            c == '\'' || c == '*' || c == '+' || c == '.' || c == '^' || c == '`' || c == '|' ||
            c == '~') {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

TqPolicyValidationResult
validate_taperquery_route_policy_ir(const TqRoutePolicy& route,
                                    const TqPolicyValidationOptions& options) {
    TqPolicyValidationResult result;

    // 1. Route base validation
    if (route.route_id.empty()) {
        if (!add_issue(result, TqPolicyValidationSeverity::Error,
                       TqPolicyValidationSubsystem::Route, "", "route_id", "EMPTY_ROUTE_ID",
                       "route_id must not be empty",
                       "Assign a unique route_id for this route block.", options)) {
            return result;
        }
    }
    if (route.match_prefix.empty()) {
        if (!add_issue(result, TqPolicyValidationSeverity::Error,
                       TqPolicyValidationSubsystem::Match, route.route_id, "match_prefix",
                       "EMPTY_MATCH_PATH", "match_prefix must not be empty",
                       "Specify a path starting with '/' for request routing.", options)) {
            return result;
        }
    } else if (route.match_prefix[0] != '/') {
        if (!add_issue(result, TqPolicyValidationSeverity::Error,
                       TqPolicyValidationSubsystem::Match, route.route_id, "match_prefix",
                       "INVALID_MATCH_PATH_PREFIX", "match_prefix must start with '/'",
                       "Prepend a '/' to the path (e.g. '/api' instead of 'api').", options)) {
            return result;
        }
    }

    // 1b. Enum value validation
    if (static_cast<std::uint8_t>(route.match_kind) > 1) {
        if (!add_issue(result, TqPolicyValidationSeverity::Error,
                       TqPolicyValidationSubsystem::Route, route.route_id, "match_kind",
                       "INVALID_ROUTE_MATCH_KIND", "match_kind contains an invalid enum value",
                       "Use a supported match_kind (prefix or exact).", options)) {
            return result;
        }
    }
    if (static_cast<std::uint8_t>(route.mutation) > 2) {
        if (!add_issue(result, TqPolicyValidationSeverity::Error,
                       TqPolicyValidationSubsystem::Route, route.route_id, "mutation",
                       "INVALID_MUTATION_MODE", "mutation contains an invalid enum value",
                       "Use a supported mutation mode (disabled, headers_only, or full).",
                       options)) {
            return result;
        }
    }
    if (static_cast<std::uint8_t>(route.allowed_method) > 5) {
        if (!add_issue(result, TqPolicyValidationSeverity::Error,
                       TqPolicyValidationSubsystem::Route, route.route_id, "allowed_method",
                       "INVALID_HTTP_METHOD", "allowed_method contains an invalid enum value",
                       "Use a supported HTTP method (any, get, post, put, delete, patch).",
                       options)) {
            return result;
        }
    }
    if (static_cast<std::uint8_t>(route.failure_mode) > 1) {
        if (!add_issue(result, TqPolicyValidationSeverity::Error,
                       TqPolicyValidationSubsystem::Route, route.route_id, "failure_mode",
                       "INVALID_FAILURE_MODE", "failure_mode contains an invalid enum value",
                       "Use a supported failure_mode (fail_open or fail_closed).", options)) {
            return result;
        }
    }

    // 2. Cache validation (Store behavior require cache layers, TTLs, GET/ANY methods, vary
    // headers, auth, etc.)
    if (route.cache.enabled) {
        bool is_store_behavior = (route.cache.behavior != TqCacheBehavior::Bypass);

        if (is_store_behavior) {
            if (route.cache.ttl_ms == 0) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Cache, route.route_id, "cache.ttl_ms",
                               "CACHE_STORE_MISSING_TTL",
                               "cache.ttl_ms is required when cache is enabled with store behavior",
                               "Assign a positive non-zero cache TTL value (e.g. 30000).",
                               options)) {
                    return result;
                }
            }

            if (route.allowed_method != TqHttpMethod::Get &&
                route.allowed_method != TqHttpMethod::Any) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Cache, route.route_id, "cache",
                               "CACHE_STORE_INVALID_METHOD",
                               "cache store behavior is only supported for GET or ANY routes",
                               "Remove cache block or change allowed_method to GET or ANY.",
                               options)) {
                    return result;
                }
            }

            if (options.strict_production && !route.cache.l1.enabled && !route.cache.l2.enabled) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Cache, route.route_id, "cache",
                               "CACHE_STORE_MISSING_LAYERS",
                               "at least one cache layer (l1 or l2) must be enabled in strict "
                               "production mode",
                               "Set l1.enabled=true or l2.enabled=true inside cache configuration.",
                               options)) {
                    return result;
                }
            }
        }

        if (route.cache.l1.enabled) {
            if (route.cache.l1.capacity_entries == 0) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Cache, route.route_id,
                               "cache.l1.capacity_entries", "L1_CACHE_ZERO_CAPACITY",
                               "L1 cache capacity must be greater than zero when enabled",
                               "Specify a positive entry capacity (e.g. 1000).", options)) {
                    return result;
                }
            }
        }

        if (route.cache.l2.enabled) {
            if (route.cache.l2.path.empty()) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Cache, route.route_id, "cache.l2.path",
                               "L2_CACHE_MISSING_PATH",
                               "L2 cache path is required when L2 cache is enabled",
                               "Specify a valid local storage path or RocksDB directory path.",
                               options)) {
                    return result;
                }
            } else if (route.cache.l2.path.length() > 255) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Cache, route.route_id, "cache.l2.path",
                               "L2_CACHE_PATH_TOO_LONG",
                               "L2 cache path length exceeds the maximum runtime limit (255 chars)",
                               "Use a shorter storage path prefix for the L2 RocksDB instance.",
                               options)) {
                    return result;
                }
            }
        }

        if (route.cache.private_cache.enabled) {
            if (route.cache.private_cache.auth_scope_header.empty()) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Cache, route.route_id,
                               "cache.private_cache.auth_scope_header",
                               "PRIVATE_CACHE_MISSING_AUTH_HEADER",
                               "private_cache requires auth_scope_header to be set",
                               "Specify the header containing authorization context (e.g. "
                               "'Authorization').",
                               options)) {
                    return result;
                }
            } else if (!is_valid_http_header_name(route.cache.private_cache.auth_scope_header)) {
                if (!add_issue(
                        result, TqPolicyValidationSeverity::Error,
                        TqPolicyValidationSubsystem::Cache, route.route_id,
                        "cache.private_cache.auth_scope_header",
                        "PRIVATE_CACHE_INVALID_AUTH_HEADER_CHARACTERS",
                        "auth_scope_header contains invalid HTTP header characters",
                        "Use a valid HTTP header name matching alphanumeric and compliant symbols.",
                        options)) {
                    return result;
                }
            }
        }

        // Vary header uniqueness validation after normalization (must fail with ERROR per findings
        // comment 3)
        if (!route.cache.vary_headers.names.empty()) {
            std::set<std::string> seen_headers;
            for (const auto& original_name : route.cache.vary_headers.names) {
                if (original_name.empty()) {
                    if (!add_issue(result, TqPolicyValidationSeverity::Error,
                                   TqPolicyValidationSubsystem::Cache, route.route_id,
                                   "cache.vary_headers.names", "VARY_HEADER_EMPTY",
                                   "Vary header names must not be empty",
                                   "Remove empty strings from the vary headers declaration list.",
                                   options)) {
                        return result;
                    }
                    continue;
                }
                std::string lowered = original_name;
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
                if (!seen_headers.insert(lowered).second) {
                    if (!add_issue(result, TqPolicyValidationSeverity::Error,
                                   TqPolicyValidationSubsystem::Cache, route.route_id,
                                   "cache.vary_headers.names", "VARY_HEADER_DUPLICATE",
                                   "duplicate vary header name after lowercase normalization: '" +
                                       lowered + "'",
                                   "Vary headers must be unique. Remove redundant elements.",
                                   options)) {
                        return result;
                    }
                }
            }
        }
    }

    // 3. Field Filter Validation
    if (route.field_filter.mode == TqFieldFilterMode::None) {
        if (!route.field_filter.fields.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::FieldFilter, route.route_id,
                           "field_filter.fields", "NONE_MODE_WITH_FIELDS",
                           "None mode must not contain fields",
                           "Clear the fields list or change the field filter mode.", options)) {
                return result;
            }
        }
    } else {
        if (route.field_filter.mode == TqFieldFilterMode::Allowlist &&
            route.field_filter.fields.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::FieldFilter, route.route_id,
                           "field_filter.fields", "ALLOWLIST_EMPTY_FIELDS",
                           "allowlist mode requires at least one field to filter",
                           "Specify field names to keep in the final response.", options)) {
                return result;
            }
        }
        if (route.field_filter.mode == TqFieldFilterMode::Denylist &&
            route.field_filter.fields.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::FieldFilter, route.route_id,
                           "field_filter.fields", "DENYLIST_EMPTY_FIELDS",
                           "denylist mode requires at least one field to redact",
                           "Specify field names to remove from the final response.", options)) {
                return result;
            }
        }

        std::set<std::string> seen_fields;
        for (const auto& field : route.field_filter.fields) {
            if (field.empty()) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::FieldFilter, route.route_id,
                               "field_filter.fields", "FIELD_FILTER_EMPTY_FIELD",
                               "field filter names must not be empty",
                               "Remove empty fields from the filtering declarations list.",
                               options)) {
                    return result;
                }
            } else if (field.length() > 64) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::FieldFilter, route.route_id,
                               "field_filter.fields", "FIELD_FILTER_NAME_TOO_LONG",
                               "field name exceeds runtime limit of 64 chars",
                               "Simplify the json path or field selector name.", options)) {
                    return result;
                }
            }
            if (options.strict_production && !seen_fields.insert(field).second) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::FieldFilter, route.route_id,
                               "field_filter.fields", "FIELD_FILTER_DUPLICATE_FIELD",
                               "duplicate field found in filtering block: '" + field + "'",
                               "Remove duplicate field references.", options)) {
                    return result;
                }
            }
        }

        if (route.field_filter.fields.size() > 16) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::FieldFilter, route.route_id,
                           "field_filter.fields", "FIELD_FILTER_LIMIT_EXCEEDED",
                           "too many field_filter fields (exceeds runtime limit of 16)",
                           "Consolidate selectors or nesting structures.", options)) {
                return result;
            }
        }
    }

    // 4. Field Variant Cache validation
    if (route.cache.field_variant.enabled) {
        if (!route.cache.enabled) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Cache, route.route_id,
                           "cache.field_variant", "FVC_WITHOUT_CACHE",
                           "field variant cache requires cache to be enabled",
                           "Enable cache in this route policy block.", options)) {
                return result;
            }
        }
        const auto& fvc = route.cache.field_variant;
        if (fvc.max_variants_per_route == 0) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Cache, route.route_id,
                           "cache.field_variant.max_variants_per_route", "FVC_ZERO_MAX_VARIANTS",
                           "max_variants_per_route must be greater than zero when field variant "
                           "cache is enabled",
                           "Assign a positive integer boundary.", options)) {
                return result;
            }
        }
        if (fvc.min_field_count > fvc.max_field_count) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Cache, route.route_id,
                           "cache.field_variant.min_field_count", "FVC_MIN_EXCEEDS_MAX",
                           "min_field_count must be less than or equal to max_field_count",
                           "Correct the field count boundaries.", options)) {
                return result;
            }
        }
        if (fvc.admission_threshold == 0) {
            if (!add_issue(
                    result, TqPolicyValidationSeverity::Error, TqPolicyValidationSubsystem::Cache,
                    route.route_id, "cache.field_variant.admission_threshold",
                    "FVC_ZERO_ADMISSION_THRESHOLD", "admission_threshold must be greater than zero",
                    "Specify an admission hit threshold.", options)) {
                return result;
            }
        }
        if (fvc.ttl_max_ms == 0) {
            if (!add_issue(
                    result, TqPolicyValidationSeverity::Error, TqPolicyValidationSubsystem::Cache,
                    route.route_id, "cache.field_variant.ttl_max_ms", "FVC_ZERO_TTL_MAX",
                    "ttl_max_ms must be greater than zero when field variant cache is enabled",
                    "Assign a positive TTL for variants.", options)) {
                return result;
            }
        }
    }

    // 5. Pagination Validation
    if (route.pagination.enabled) {
        if (route.pagination.mode == TqPaginationMode::Cursor) {
            if (!add_issue(
                    result, TqPolicyValidationSeverity::Error,
                    TqPolicyValidationSubsystem::Pagination, route.route_id, "pagination.mode",
                    "PAGINATION_CURSOR_UNSUPPORTED",
                    "cursor mode is explicitly marked as unsupported until runtime support exists",
                    "Change pagination.mode to limit_offset or disable pagination.", options)) {
                return result;
            }
        }
        if (route.pagination.mode == TqPaginationMode::None) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Pagination, route.route_id,
                           "pagination.mode", "PAGINATION_MODE_REQUIRED",
                           "pagination.mode required when pagination is enabled",
                           "Assign a supported pagination mode (e.g. limit_offset).", options)) {
                return result;
            }
        }
        if (route.pagination.default_limit == 0) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Pagination, route.route_id,
                           "pagination.default_limit", "PAGINATION_ZERO_DEFAULT_LIMIT",
                           "pagination.default_limit required when pagination is enabled",
                           "Assign a positive default limit (e.g. 20).", options)) {
                return result;
            }
        }
        if (route.pagination.max_limit == 0) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Pagination, route.route_id,
                           "pagination.max_limit", "PAGINATION_ZERO_MAX_LIMIT",
                           "pagination.max_limit required when pagination is enabled",
                           "Assign a positive maximum limit (e.g. 100).", options)) {
                return result;
            }
        }
        if (route.pagination.max_limit > 0 &&
            route.pagination.max_limit < route.pagination.default_limit) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Pagination, route.route_id,
                           "pagination.max_limit", "PAGINATION_MAX_LESS_THAN_DEFAULT",
                           "pagination.max_limit must not be less than default_limit",
                           "Increase the max_limit or reduce the default_limit.", options)) {
                return result;
            }
        }
        if (route.pagination.limit_param.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Pagination, route.route_id,
                           "pagination.limit_param", "PAGINATION_EMPTY_LIMIT_PARAM",
                           "pagination.limit_param must not be empty",
                           "Specify the query parameter for limit control (e.g. 'limit').",
                           options)) {
                return result;
            }
        }
        if (route.pagination.offset_param.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Pagination, route.route_id,
                           "pagination.offset_param", "PAGINATION_EMPTY_OFFSET_PARAM",
                           "pagination.offset_param must not be empty",
                           "Specify the query parameter for offset control (e.g. 'offset').",
                           options)) {
                return result;
            }
        }
        if (route.allowed_method != TqHttpMethod::Any &&
            route.allowed_method != TqHttpMethod::Get) {
            if (!add_issue(
                    result, TqPolicyValidationSeverity::Error,
                    TqPolicyValidationSubsystem::Pagination, route.route_id, "pagination",
                    "PAGINATION_INVALID_METHOD", "pagination only supported for GET or ANY routes",
                    "Remove pagination block or change allowed_method to GET or ANY.", options)) {
                return result;
            }
        }
    }

    // 6. Compression Validation
    if (route.compression.enabled) {
        if (route.compression.min_size_bytes == 0) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Compression, route.route_id,
                           "compression.min_size_bytes", "COMPRESSION_ZERO_MIN_SIZE",
                           "compression enabled with min_size_bytes <= 0",
                           "Specify a safe minimum payload size threshold in bytes (e.g. 1024).",
                           options)) {
                return result;
            }
        }
        if (route.compression.eligible_content_types.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Compression, route.route_id,
                           "compression.eligible_content_types", "COMPRESSION_EMPTY_CONTENT_TYPES",
                           "compression enabled with empty eligible_content_types",
                           "Add at least one content type (e.g. 'application/json').", options)) {
                return result;
            }
        }
        if (route.compression.preferred_algorithms.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Compression, route.route_id,
                           "compression.preferred_algorithms", "COMPRESSION_EMPTY_ALGORITHMS",
                           "compression enabled with no preferred_algorithms",
                           "Declare at least one compression algorithm (e.g. GZIP, Brotli).",
                           options)) {
                return result;
            }
        }

        std::set<TqCompressionAlgorithm> seen_algos;
        for (const auto& algo : route.compression.preferred_algorithms) {
            if (algo == TqCompressionAlgorithm::None) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Compression, route.route_id,
                               "compression.preferred_algorithms", "COMPRESSION_ALGORITHM_NONE",
                               "compression.preferred_algorithms contains unsupported 'none' value",
                               "Remove 'none' from preferred list.", options)) {
                    return result;
                }
            }
            if (static_cast<std::uint8_t>(algo) > 3) {
                if (!add_issue(
                        result, TqPolicyValidationSeverity::Error,
                        TqPolicyValidationSubsystem::Compression, route.route_id,
                        "compression.preferred_algorithms", "COMPRESSION_ALGORITHM_INVALID",
                        "compression.preferred_algorithms contains an invalid or unknown algorithm",
                        "Use a supported compression algorithm (gzip, brotli, or zstd).",
                        options)) {
                    return result;
                }
            }
            if (!seen_algos.insert(algo).second) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Compression, route.route_id,
                               "compression.preferred_algorithms",
                               "COMPRESSION_DUPLICATE_ALGORITHM", "duplicate algorithm is rejected",
                               "Remove the duplicate algorithm from preference ordering list.",
                               options)) {
                    return result;
                }
            }
        }

        if (route.compression.eligible_content_types.size() > 8) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Compression, route.route_id,
                           "compression.eligible_content_types", "COMPRESSION_CONTENT_TYPES_LIMIT",
                           "eligible content types list exceeds the maximum runtime capacity (8)",
                           "Reduce content types matching or aggregate formats.", options)) {
                return result;
            }
        }

        for (const auto& ct : route.compression.eligible_content_types) {
            if (ct.length() > 64) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::Compression, route.route_id,
                               "compression.eligible_content_types",
                               "COMPRESSION_CONTENT_TYPE_TOO_LONG",
                               "content type name length exceeds maximum limit of 64 characters",
                               "Simplify content type selector name.", options)) {
                    return result;
                }
            }
        }

        if (route.compression.already_encoded_behavior != TqAlreadyEncodedBehavior::Skip) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Compression, route.route_id,
                           "compression.already_encoded_behavior",
                           "COMPRESSION_INVALID_ENCODED_BEHAVIOR",
                           "compression.already_encoded_behavior must be 'skip'",
                           "Change the encoding behavior setting to 'skip'.", options)) {
                return result;
            }
        }
    }

    // 7. Coalescing Validation
    if (route.coalescing.enabled) {
        if (route.coalescing.mode != TqCoalescingMode::CacheAssisted) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.mode", "COALESCING_MODE_REQUIRED",
                           "coalescing enabled requires mode to be cache-assisted",
                           "Set coalescing mode to cache-assisted.", options)) {
                return result;
            }
        }
        if (route.coalescing.backend_timeout_ms == 0) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.backend_timeout_ms", "COALESCING_ZERO_TIMEOUT",
                           "backend_timeout_ms must be > 0",
                           "Assign a positive timeout value in milliseconds (e.g. 5000).",
                           options)) {
                return result;
            }
        }
        if (route.coalescing.backend_timeout_ms > 30000) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.backend_timeout_ms", "COALESCING_TIMEOUT_EXCEEDED",
                           "backend_timeout_ms must be <= 30000 (production maximum)",
                           "Reduce backend_timeout_ms within normal system limits.", options)) {
                return result;
            }
        }
        if (route.coalescing.handoff_buffer_ms == 0) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.handoff_buffer_ms", "COALESCING_ZERO_HANDOFF",
                           "handoff_buffer_ms must be > 0",
                           "Assign a positive handoff buffer value.", options)) {
                return result;
            }
        }
        if (route.coalescing.handoff_buffer_ms > 5000) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.handoff_buffer_ms", "COALESCING_HANDOFF_EXCEEDED",
                           "handoff_buffer_ms must be <= 5000",
                           "Reduce the handoff buffer to a safe system bound.", options)) {
                return result;
            }
        }
        if (route.coalescing.backend_timeout_ms + route.coalescing.handoff_buffer_ms > 30000) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id, "coalescing",
                           "COALESCING_BUDGET_EXCEEDED",
                           "coalescing total wait budget (timeout + handoff) exceeds follower "
                           "maximum (30000)",
                           "Rebalance coalescing timeouts or follower limits.", options)) {
                return result;
            }
        }
        if (route.coalescing.handoff_buffer_ms > route.coalescing.backend_timeout_ms) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.handoff_buffer_ms", "COALESCING_HANDOFF_EXCEEDS_TIMEOUT",
                           "coalescing handoff_buffer_ms exceeds backend_timeout_ms",
                           "Set handoff_buffer_ms to be less than or equal to backend_timeout_ms.",
                           options)) {
                return result;
            }
        }
        if (route.coalescing.result_ready_retention_ms < 10) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.result_ready_retention_ms", "COALESCING_RETENTION_TOO_SHORT",
                           "result_ready_retention_ms must be >= 10",
                           "Specify a larger retention time.", options)) {
                return result;
            }
        }
        if (route.coalescing.result_ready_retention_ms > 1000) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.result_ready_retention_ms", "COALESCING_RETENTION_EXCEEDED",
                           "result_ready_retention_ms must be <= 1000",
                           "Set retention within the 10-1000ms supported range.", options)) {
                return result;
            }
        }
        if (route.coalescing.result_ready_retention_ms >= route.coalescing.backend_timeout_ms) {
            if (!add_issue(
                    result, TqPolicyValidationSeverity::Error,
                    TqPolicyValidationSubsystem::Coalescing, route.route_id,
                    "coalescing.result_ready_retention_ms", "COALESCING_RETENTION_EXCEEDS_TIMEOUT",
                    "result_ready_retention_ms must be less than backend_timeout_ms",
                    "Reduce result_ready_retention_ms relative to backend_timeout_ms.", options)) {
                return result;
            }
        }
        if (route.coalescing.max_waiters_per_key == 0) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.max_waiters_per_key", "COALESCING_ZERO_MAX_WAITERS",
                           "coalescing.max_waiters_per_key must be > 0",
                           "Set max waiters limit to a value greater than 0.", options)) {
                return result;
            }
        }
        if (route.coalescing.max_follower_wait_budget_ms > 0 &&
            route.coalescing.max_follower_wait_budget_ms < route.coalescing.handoff_buffer_ms) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.max_follower_wait_budget_ms",
                           "COALESCING_FOLLOWER_BUDGET_TOO_LOW",
                           "coalescing max_follower_wait_budget_ms must be >= handoff_buffer_ms",
                           "Assign a compliant follower wait budget.", options)) {
                return result;
            }
        }
        if (route.coalescing.max_active_follower_waiters > 0 &&
            route.coalescing.max_active_follower_waiters_per_shard >
                route.coalescing.max_active_follower_waiters) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Coalescing, route.route_id,
                           "coalescing.max_active_follower_waiters_per_shard",
                           "COALESCING_SHARD_EXCEEDS_GLOBAL",
                           "coalescing max_active_follower_waiters_per_shard must be <= "
                           "max_active_follower_waiters",
                           "Align the shard level limits to be lower than global waiters limits.",
                           options)) {
                return result;
            }
        }
        if (route.coalescing.require_cache_enabled) {
            if (!route.cache.enabled) {
                if (!add_issue(
                        result, TqPolicyValidationSeverity::Error,
                        TqPolicyValidationSubsystem::Coalescing, route.route_id, "coalescing",
                        "COALESCING_MISSING_CACHE",
                        "coalescing requires cache to be enabled when require_cache_enabled is set",
                        "Enable cache inside this route policy block.", options)) {
                    return result;
                }
            }
        }
        if (route.coalescing.allow_authenticated) {
            if (route.cache.private_cache.auth_scope_header.empty()) {
                if (!add_issue(
                        result, TqPolicyValidationSeverity::Error,
                        TqPolicyValidationSubsystem::Coalescing, route.route_id,
                        "coalescing.allow_authenticated", "COALESCING_AUTHENTICATED_MISSING_SCOPE",
                        "coalescing.allow_authenticated=true requires cache.auth_scope_header to "
                        "be set",
                        "Configure private_cache auth_scope_header inside cache.", options)) {
                    return result;
                }
            }
        }
        if (route.allowed_method != TqHttpMethod::Any &&
            route.allowed_method != TqHttpMethod::Get) {
            if (!add_issue(
                    result, TqPolicyValidationSeverity::Error,
                    TqPolicyValidationSubsystem::Coalescing, route.route_id, "coalescing",
                    "COALESCING_INVALID_METHOD", "coalescing only supported for GET or ANY routes",
                    "Remove coalescing block or change allowed_method to GET or ANY.", options)) {
                return result;
            }
        }
    }

    // 8. Cross-feature validation
    bool is_body_transform_enabled =
        (route.field_filter.mode != TqFieldFilterMode::None) || route.pagination.enabled ||
        route.compression.enabled ||
        (route.cache.enabled && route.cache.behavior == TqCacheBehavior::Store) ||
        route.coalescing.enabled;

    if (is_body_transform_enabled) {
        if (route.mutation == TqMutationMode::Disabled) {
            if (!add_issue(result, TqPolicyValidationSeverity::Warning,
                           TqPolicyValidationSubsystem::CrossFeature, route.route_id, "mutation",
                           "OBSERVE_MODE_WARNING",
                           "feature block declared while mutation=disabled; observe-only behavior "
                           "expected",
                           "Be aware that this route runs strictly as dry-run/warnings only.",
                           options)) {
                return result;
            }
        } else if (route.mutation == TqMutationMode::HeadersOnly) {
            if (route.field_filter.mode != TqFieldFilterMode::None) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::CrossFeature, route.route_id,
                               "mutation", "HEADERS_ONLY_WITH_FIELD_FILTER",
                               "field_filter cannot be combined with headers-only mutation mode",
                               "Use mutation=full or remove field filters.", options)) {
                    return result;
                }
            }
            if (route.pagination.enabled) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::CrossFeature, route.route_id,
                               "mutation", "HEADERS_ONLY_WITH_PAGINATION",
                               "pagination cannot be combined with headers-only mutation mode",
                               "Use mutation=full or remove pagination.", options)) {
                    return result;
                }
            }
            if (route.compression.enabled) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::CrossFeature, route.route_id,
                               "mutation", "HEADERS_ONLY_WITH_COMPRESSION",
                               "compression cannot be combined with headers-only mutation mode",
                               "Use mutation=full or remove compression.", options)) {
                    return result;
                }
            }
            if (route.cache.enabled && route.cache.behavior == TqCacheBehavior::Store) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::CrossFeature, route.route_id,
                               "mutation", "HEADERS_ONLY_WITH_CACHE_STORE",
                               "cache store cannot be combined with headers-only mutation mode",
                               "Use mutation=full or change cache behavior to bypass.", options)) {
                    return result;
                }
            }
            if (route.coalescing.enabled) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::CrossFeature, route.route_id,
                               "mutation", "HEADERS_ONLY_WITH_COALESCING",
                               "coalescing cannot be combined with headers-only mutation mode",
                               "Use mutation=full or remove coalescing.", options)) {
                    return result;
                }
            }
        }
    }

    return result;
}

TqPolicyValidationResult validate_taperquery_policy_ir(const TqPolicyDocument& policy,
                                                       const TqPolicyValidationOptions& options) {
    TqPolicyValidationResult result;

    // 1. Production strict document checking
    if (options.strict_production) {
        if (policy.document_id.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Document, "", "document_id",
                           "MISSING_DOCUMENT_ID",
                           "document_id is required in strict production mode",
                           "Set a unique string document_id in the policy document.", options)) {
                return result;
            }
        }
        if (policy.routes.empty()) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Document, "", "routes", "EMPTY_ROUTES_LIST",
                           "route list must not be empty in strict production mode",
                           "Add at least one route block to the policy document.", options)) {
                return result;
            }
        }
    }

    // 2. Version validation
    auto ver_res = validate_policy_version_info(policy.version);
    if (!ver_res.ok) {
        for (const auto& issue : ver_res.issues) {
            if (!add_issue(result, TqPolicyValidationSeverity::Error,
                           TqPolicyValidationSubsystem::Version, "", issue.field,
                           "UNSUPPORTED_VERSION_PARAMETER", issue.reason,
                           "Specify compliant/supported version identifiers.", options)) {
                return result;
            }
        }
    }

    // 3. Validate individual routes
    for (const auto& route : policy.routes) {
        auto route_res = validate_taperquery_route_policy_ir(route, options);
        for (const auto& issue : route_res.issues) {
            result.issues.push_back(issue);
        }
        if (!route_res.ok) {
            result.ok = false;
            if (!options.collect_all) {
                return result;
            }
        }
    }

    // 4. Cross-route checks (duplicate route ids)
    for (std::size_t i = 0; i < policy.routes.size(); ++i) {
        const auto& r1 = policy.routes[i];
        if (r1.route_id.empty()) {
            continue;
        }
        for (std::size_t j = i + 1; j < policy.routes.size(); ++j) {
            const auto& r2 = policy.routes[j];
            if (r1.route_id == r2.route_id) {
                if (!add_issue(result, TqPolicyValidationSeverity::Error,
                               TqPolicyValidationSubsystem::CrossRoute, r2.route_id, "route_id",
                               "DUPLICATE_ROUTE_ID",
                               "duplicate route id '" + r2.route_id + "' is rejected",
                               "Assign a unique route_id for each route.", options)) {
                    return result;
                }
            }
        }
    }

    // 5. Run dedicated route analysis for overlaps, duplicates, shadowing, L2 warnings
    auto analysis = analyze_taperquery_route_precedence(policy);
    for (const auto& finding : analysis.findings) {
        TqPolicyValidationSeverity sev = TqPolicyValidationSeverity::Error;
        if (finding.severity == TqRouteAnalysisSeverity::Warning) {
            sev = TqPolicyValidationSeverity::Warning;
        } else if (finding.severity == TqRouteAnalysisSeverity::Info) {
            sev = TqPolicyValidationSeverity::Warning; // Map info findings to warnings in semantic
                                                       // validation
        }

        std::string code_str;
        switch (finding.kind) {
        case TqRouteAnalysisKind::DuplicateRouteId:
            code_str = "ROUTE_ANALYSIS_DUPLICATE_ID";
            break;
        case TqRouteAnalysisKind::DuplicateExactMatch:
            code_str = "ROUTE_ANALYSIS_DUPLICATE_EXACT_MATCH";
            break;
        case TqRouteAnalysisKind::EquivalentPrefixMatch:
            code_str = "ROUTE_ANALYSIS_EQUIVALENT_PREFIX_MATCH";
            break;
        case TqRouteAnalysisKind::PrefixShadowsPrefix:
            code_str = "ROUTE_ANALYSIS_PREFIX_SHADOWS_PREFIX";
            break;
        case TqRouteAnalysisKind::PrefixShadowsExact:
            code_str = "ROUTE_ANALYSIS_PREFIX_SHADOWS_EXACT";
            break;
        case TqRouteAnalysisKind::EarlierRouteWins:
            code_str = "ROUTE_ANALYSIS_EARLIER_ROUTE_WINS";
            break;
        case TqRouteAnalysisKind::SharedL2CachePath:
            code_str = "ROUTE_ANALYSIS_SHARED_L2_PATH";
            break;
        }

        if (!add_issue(result, sev, TqPolicyValidationSubsystem::CrossRoute, finding.route_id,
                       finding.field_path, code_str, finding.reason, finding.hint, options)) {
            return result;
        }
    }

    return result;
}

} // namespace bytetaper::taperquery

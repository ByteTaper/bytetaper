// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/policy_semantic_validator.h"

#include "policy/cache_policy_validator.h"
#include "policy/coalescing_policy_validator.h"
#include "policy/compression_policy.h"
#include "policy/pagination_policy.h"

#include <cstring>

namespace bytetaper::policy {

namespace {

static bool add_issue(PolicyValidationResult* result, PolicyValidationSeverity severity,
                      PolicyValidationSubsystem subsystem, const char* route_id, const char* field,
                      const char* reason, const PolicyValidationOptions& options) {

    if (severity == PolicyValidationSeverity::Warning && !options.include_warnings) {
        return true;
    }

    if (severity == PolicyValidationSeverity::Error) {
        result->ok = false;
    }

    if (result->issue_count >= kMaxPolicyValidationIssues) {
        return options.collect_all;
    }

    if (result->issue_count == kMaxPolicyValidationIssues - 1) {
        PolicyValidationIssue& overflow = result->issues[result->issue_count++];
        overflow.severity = PolicyValidationSeverity::Error;
        overflow.subsystem = PolicyValidationSubsystem::Route;
        overflow.route_id = route_id;
        overflow.field = "";
        overflow.reason = "semantic validation issue capacity exceeded";
        result->ok = false;
        return options.collect_all;
    }

    PolicyValidationIssue& issue = result->issues[result->issue_count++];
    issue.severity = severity;
    issue.subsystem = subsystem;
    issue.route_id = route_id;
    issue.field = field;
    issue.reason = reason;

    if (severity == PolicyValidationSeverity::Error && !options.collect_all) {
        return false;
    }
    return true;
}

static bool methods_overlap(HttpMethod m1, HttpMethod m2) {
    return m1 == HttpMethod::Any || m2 == HttpMethod::Any || m1 == m2;
}

static bool is_prefix_shadow(const char* broad, const char* narrow) {
    if (broad == nullptr || narrow == nullptr) {
        return false;
    }
    std::size_t broad_len = std::strlen(broad);
    std::size_t narrow_len = std::strlen(narrow);
    if (narrow_len < broad_len) {
        return false;
    }
    if (std::strncmp(broad, narrow, broad_len) != 0) {
        return false;
    }
    if (broad_len == 0) {
        return true;
    }
    if (broad[broad_len - 1] == '/') {
        return true;
    }
    if (narrow[broad_len] == '\0' || narrow[broad_len] == '/') {
        return true;
    }
    return false;
}

} // namespace

bool validate_route_policy_semantic(const RoutePolicy& route, PolicyValidationResult* result,
                                    const PolicyValidationOptions& options) {

    // 1. Route base validation
    if (route.route_id == nullptr || route.route_id[0] == '\0') {
        if (!add_issue(result, PolicyValidationSeverity::Error, PolicyValidationSubsystem::Route,
                       route.route_id, "route_id", "route_id is required", options)) {
            return false;
        }
    }
    if (route.match_prefix == nullptr || route.match_prefix[0] != '/') {
        if (!add_issue(result, PolicyValidationSeverity::Error, PolicyValidationSubsystem::Route,
                       route.route_id, "match_prefix", "match_prefix must start with '/'",
                       options)) {
            return false;
        }
    }

    // Validate enum ranges just in case
    if (static_cast<std::uint8_t>(route.allowed_method) > 5) {
        if (!add_issue(result, PolicyValidationSeverity::Error, PolicyValidationSubsystem::Route,
                       route.route_id, "method", "method enum is unrecognized", options)) {
            return false;
        }
    }
    if (static_cast<std::uint8_t>(route.mutation) > 2) {
        if (!add_issue(result, PolicyValidationSeverity::Error, PolicyValidationSubsystem::Route,
                       route.route_id, "mutation", "mutation enum is unrecognized", options)) {
            return false;
        }
    }
    if (static_cast<std::uint8_t>(route.failure_mode) > 1) {
        if (!add_issue(result, PolicyValidationSeverity::Error, PolicyValidationSubsystem::Route,
                       route.route_id, "failure_mode", "failure_mode enum is unrecognized",
                       options)) {
            return false;
        }
    }

    // 2. Cache safe validation
    if (const char* err = validate_cache_policy_safe(route.cache, route.allowed_method)) {
        if (!add_issue(result, PolicyValidationSeverity::Error, PolicyValidationSubsystem::Cache,
                       route.route_id, "cache", err, options)) {
            return false;
        }
    }

    // 3. Compression safe validation
    if (const char* err = validate_compression_policy_safe(route.compression)) {
        if (!add_issue(result, PolicyValidationSeverity::Error,
                       PolicyValidationSubsystem::Compression, route.route_id, "compression", err,
                       options)) {
            return false;
        }
    }

    // 4. Pagination safe validation
    if (const char* err = validate_pagination_policy_safe(route.pagination)) {
        if (!add_issue(result, PolicyValidationSeverity::Error,
                       PolicyValidationSubsystem::Pagination, route.route_id, "pagination", err,
                       options)) {
            return false;
        }
    }

    // 5. Coalescing safe validation
    if (const char* err = validate_coalescing_policy_safe(route.coalescing, &route.cache)) {
        if (!add_issue(result, PolicyValidationSeverity::Error,
                       PolicyValidationSubsystem::Coalescing, route.route_id, "coalescing", err,
                       options)) {
            return false;
        }
    }

    // 6. Cross-feature validation
    bool is_body_transform_enabled =
        (route.field_filter.mode != FieldFilterMode::None) || route.pagination.enabled ||
        route.compression.enabled ||
        (route.cache.enabled && route.cache.behavior == CacheBehavior::Store) ||
        route.coalescing.enabled;

    if (is_body_transform_enabled) {
        if (route.mutation == MutationMode::Disabled) {
            if (!add_issue(result, PolicyValidationSeverity::Warning,
                           PolicyValidationSubsystem::Route, route.route_id, "mutation",
                           "feature block declared while mutation=disabled; observe-only behavior "
                           "expected",
                           options)) {
                return false;
            }
        } else if (route.mutation == MutationMode::HeadersOnly) {
            if (route.field_filter.mode != FieldFilterMode::None) {
                if (!add_issue(result, PolicyValidationSeverity::Error,
                               PolicyValidationSubsystem::Route, route.route_id, "mutation",
                               "field_filter requires mutation=full", options)) {
                    return false;
                }
            }
            if (route.pagination.enabled) {
                if (!add_issue(result, PolicyValidationSeverity::Error,
                               PolicyValidationSubsystem::Route, route.route_id, "mutation",
                               "pagination requires mutation=full", options)) {
                    return false;
                }
            }
            if (route.compression.enabled) {
                if (!add_issue(result, PolicyValidationSeverity::Error,
                               PolicyValidationSubsystem::Route, route.route_id, "mutation",
                               "compression requires mutation=full", options)) {
                    return false;
                }
            }
            if (route.cache.enabled && route.cache.behavior == CacheBehavior::Store) {
                if (!add_issue(result, PolicyValidationSeverity::Error,
                               PolicyValidationSubsystem::Route, route.route_id, "mutation",
                               "cache store requires mutation=full", options)) {
                    return false;
                }
            }
            if (route.coalescing.enabled) {
                if (!add_issue(result, PolicyValidationSeverity::Error,
                               PolicyValidationSubsystem::Route, route.route_id, "mutation",
                               "coalescing requires mutation=full", options)) {
                    return false;
                }
            }
        }
    }

    if (route.coalescing.enabled && route.allowed_method != HttpMethod::Any &&
        route.allowed_method != HttpMethod::Get) {
        if (!add_issue(result, PolicyValidationSeverity::Error,
                       PolicyValidationSubsystem::Coalescing, route.route_id, "coalescing",
                       "coalescing only supported for GET or ANY routes", options)) {
            return false;
        }
    }

    if (route.pagination.enabled && route.allowed_method != HttpMethod::Any &&
        route.allowed_method != HttpMethod::Get) {
        if (!add_issue(result, PolicyValidationSeverity::Error,
                       PolicyValidationSubsystem::Pagination, route.route_id, "pagination",
                       "pagination only supported for GET or ANY routes", options)) {
            return false;
        }
    }

    return result->ok;
}

bool validate_policy_file_semantic(const PolicyFileResult& policy_file,
                                   PolicyValidationResult* result,
                                   const PolicyValidationOptions& options) {

    // 1. Validate route policy semantically for each route
    for (std::size_t i = 0; i < policy_file.count; ++i) {
        if (!validate_route_policy_semantic(policy_file.policies[i], result, options)) {
            if (!options.collect_all) {
                return false;
            }
        }
    }

    // 2. Cross-route checks
    // Check duplicate route ids
    for (std::size_t i = 0; i < policy_file.count; ++i) {
        const auto& r1 = policy_file.policies[i];
        if (r1.route_id == nullptr || r1.route_id[0] == '\0') {
            continue;
        }
        for (std::size_t j = i + 1; j < policy_file.count; ++j) {
            const auto& r2 = policy_file.policies[j];
            if (r2.route_id != nullptr && std::strcmp(r1.route_id, r2.route_id) == 0) {
                if (!add_issue(result, PolicyValidationSeverity::Error,
                               PolicyValidationSubsystem::CrossRoute, r2.route_id, "route_id",
                               "duplicate route id", options)) {
                    return false;
                }
            }
        }
    }

    // Check duplicate exact route matches
    for (std::size_t i = 0; i < policy_file.count; ++i) {
        const auto& r1 = policy_file.policies[i];
        if (r1.match_kind != RouteMatchKind::Exact || r1.match_prefix == nullptr) {
            continue;
        }
        for (std::size_t j = i + 1; j < policy_file.count; ++j) {
            const auto& r2 = policy_file.policies[j];
            if (r2.match_kind != RouteMatchKind::Exact || r2.match_prefix == nullptr) {
                continue;
            }
            if (std::strcmp(r1.match_prefix, r2.match_prefix) == 0 &&
                methods_overlap(r1.allowed_method, r2.allowed_method)) {
                if (!add_issue(result, PolicyValidationSeverity::Error,
                               PolicyValidationSubsystem::CrossRoute, r2.route_id, "match_prefix",
                               "duplicate exact route match", options)) {
                    return false;
                }
            }
        }
    }

    // Check shadowing
    for (std::size_t i = 0; i < policy_file.count; ++i) {
        const auto& r1 = policy_file.policies[i];
        if (r1.match_kind != RouteMatchKind::Prefix || r1.match_prefix == nullptr) {
            continue;
        }
        for (std::size_t j = i + 1; j < policy_file.count; ++j) {
            const auto& r2 = policy_file.policies[j];
            if (r2.match_prefix == nullptr) {
                continue;
            }

            if (methods_overlap(r1.allowed_method, r2.allowed_method)) {
                if (r2.match_kind == RouteMatchKind::Prefix) {
                    if (is_prefix_shadow(r1.match_prefix, r2.match_prefix)) {
                        if (!add_issue(result, PolicyValidationSeverity::Warning,
                                       PolicyValidationSubsystem::CrossRoute, r2.route_id,
                                       "match_prefix",
                                       "route may be shadowed by earlier prefix route", options)) {
                            return false;
                        }
                    }
                } else if (r2.match_kind == RouteMatchKind::Exact) {
                    if (is_prefix_shadow(r1.match_prefix, r2.match_prefix)) {
                        if (!add_issue(
                                result, PolicyValidationSeverity::Warning,
                                PolicyValidationSubsystem::CrossRoute, r2.route_id, "match_prefix",
                                "exact route may be shadowed by earlier prefix route", options)) {
                            return false;
                        }
                    }
                }
            }
        }
    }

    // Check shared L2 cache path warnings
    for (std::size_t i = 0; i < policy_file.count; ++i) {
        const auto& r1 = policy_file.policies[i];
        if (!r1.cache.l2.enabled || r1.cache.l2.path[0] == '\0') {
            continue;
        }
        for (std::size_t j = i + 1; j < policy_file.count; ++j) {
            const auto& r2 = policy_file.policies[j];
            if (!r2.cache.l2.enabled || r2.cache.l2.path[0] == '\0') {
                continue;
            }
            if (std::strcmp(r1.cache.l2.path, r2.cache.l2.path) == 0) {
                if (!add_issue(result, PolicyValidationSeverity::Warning,
                               PolicyValidationSubsystem::CrossRoute, r2.route_id, "cache.l2.path",
                               "multiple routes share the same L2 cache path", options)) {
                    return false;
                }
            }
        }
    }
    return result->ok;
}

bool validate_route_policy(const RoutePolicy& policy, const char** reason_out) {
    PolicyValidationResult result{};
    PolicyValidationOptions options{};
    options.collect_all = false;
    options.include_warnings = false;

    bool ok = validate_route_policy_semantic(policy, &result, options);
    if (!ok && reason_out != nullptr) {
        if (result.issue_count > 0 && result.issues[0].reason != nullptr) {
            *reason_out = result.issues[0].reason;
        } else {
            *reason_out = "unknown";
        }
    }
    return ok;
}

} // namespace bytetaper::policy

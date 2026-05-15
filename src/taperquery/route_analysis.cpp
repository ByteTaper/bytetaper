// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/route_analysis.h"

#include <algorithm>

namespace bytetaper::taperquery {

namespace {

bool methods_overlap(TqHttpMethod m1, TqHttpMethod m2) {
    return m1 == TqHttpMethod::Any || m2 == TqHttpMethod::Any || m1 == m2;
}

bool is_prefix_shadow(const std::string& broad, const std::string& narrow) {
    if (broad.empty()) {
        return true;
    }
    std::size_t broad_len = broad.length();
    std::size_t narrow_len = narrow.length();
    if (narrow_len < broad_len) {
        return false;
    }
    if (narrow.compare(0, broad_len, broad) != 0) {
        return false;
    }
    if (broad[broad_len - 1] == '/') {
        return true;
    }
    if (narrow_len == broad_len) {
        return true;
    }
    if (narrow[broad_len] == '/') {
        return true;
    }
    return false;
}

std::string method_to_string(TqHttpMethod method) {
    switch (method) {
    case TqHttpMethod::Get:
        return "GET";
    case TqHttpMethod::Post:
        return "POST";
    case TqHttpMethod::Put:
        return "PUT";
    case TqHttpMethod::Delete:
        return "DELETE";
    case TqHttpMethod::Patch:
        return "PATCH";
    default:
        return "ANY";
    }
}

std::string match_kind_to_string(TqRouteMatchKind kind) {
    switch (kind) {
    case TqRouteMatchKind::Exact:
        return "exact";
    default:
        return "prefix";
    }
}

std::string strategy_to_string(TqCacheInvalidationStrategy strategy) {
    switch (strategy) {
    case TqCacheInvalidationStrategy::RouteEpoch:
        return "RouteEpoch";
    case TqCacheInvalidationStrategy::ExactKey:
        return "ExactKey";
    case TqCacheInvalidationStrategy::Prefix:
        return "Prefix";
    default:
        return "Unknown";
    }
}

} // namespace

std::uint32_t compute_route_specificity_score(const TqRoutePolicy& route) {
    std::uint32_t score = 0;
    if (route.match_kind == TqRouteMatchKind::Exact) {
        score += 10000;
    }
    if (route.allowed_method != TqHttpMethod::Any) {
        score += 1000;
    }
    score += static_cast<std::uint32_t>(route.match_prefix.length());
    return score;
}

TqRouteAnalysisReport analyze_taperquery_route_precedence(const TqPolicyDocument& policy) {
    TqRouteAnalysisReport report;

    // 1. Build initial precedence entries
    report.precedence.reserve(policy.routes.size());
    for (std::uint32_t i = 0; i < policy.routes.size(); ++i) {
        const auto& route = policy.routes[i];
        TqRoutePrecedenceEntry entry;
        entry.route_id = route.route_id;
        entry.method = method_to_string(route.allowed_method);
        entry.match_kind = match_kind_to_string(route.match_kind);
        entry.path = route.match_prefix;
        entry.declaration_index = i;
        entry.specificity_score = compute_route_specificity_score(route);
        entry.may_shadow_later_routes = false;
        entry.may_be_shadowed = false;
        report.precedence.push_back(entry);
    }

    // 2. Perform N^2 analysis for route shadow, duplicate, and overlap checks
    for (std::size_t i = 0; i < policy.routes.size(); ++i) {
        const auto& r1 = policy.routes[i];

        // Self-check for duplicate route IDs with earlier ones
        for (std::size_t k = 0; k < i; ++k) {
            if (!r1.route_id.empty() && r1.route_id == policy.routes[k].route_id) {
                TqRouteAnalysisFinding finding;
                finding.severity = TqRouteAnalysisSeverity::Error;
                finding.kind = TqRouteAnalysisKind::DuplicateRouteId;
                finding.route_id = r1.route_id;
                finding.related_route_id = policy.routes[k].route_id;
                finding.field_path = "route_id";
                finding.reason = "Duplicate route_id '" + r1.route_id +
                                 "' detected across multiple route blocks";
                finding.hint = "Change this route_id to be unique within the policy document.";
                report.findings.push_back(finding);
                report.ok = false;
            }
        }

        for (std::size_t j = i + 1; j < policy.routes.size(); ++j) {
            const auto& r2 = policy.routes[j];

            // Method overlap check is a precondition for route interaction
            bool overlap = methods_overlap(r1.allowed_method, r2.allowed_method);

            if (overlap) {
                // Duplicate Exact / Equivalent Prefix checks
                if (r1.match_prefix == r2.match_prefix && r1.match_kind == r2.match_kind) {
                    if (r1.match_kind == TqRouteMatchKind::Exact) {
                        TqRouteAnalysisFinding finding;
                        finding.severity = TqRouteAnalysisSeverity::Error;
                        finding.kind = TqRouteAnalysisKind::DuplicateExactMatch;
                        finding.route_id = r2.route_id;
                        finding.related_route_id = r1.route_id;
                        finding.field_path = "match_prefix";
                        finding.reason = "Duplicate exact route path '" + r2.match_prefix +
                                         "' with overlapping method (" +
                                         method_to_string(r2.allowed_method) + " vs " +
                                         method_to_string(r1.allowed_method) + ")";
                        finding.hint = "Remove the duplicate route or change one of the match "
                                       "paths or allowed methods.";
                        report.findings.push_back(finding);
                        report.ok = false;

                        report.precedence[i].may_shadow_later_routes = true;
                        report.precedence[j].may_be_shadowed = true;
                    } else {
                        TqRouteAnalysisFinding finding;
                        finding.severity = TqRouteAnalysisSeverity::Warning;
                        finding.kind = TqRouteAnalysisKind::EquivalentPrefixMatch;
                        finding.route_id = r2.route_id;
                        finding.related_route_id = r1.route_id;
                        finding.field_path = "match_prefix";
                        finding.reason = "Equivalent prefix route path '" + r2.match_prefix +
                                         "' with overlapping method (" +
                                         method_to_string(r2.allowed_method) + " vs " +
                                         method_to_string(r1.allowed_method) + ")";
                        finding.hint = "Consolidate these identical prefix route definitions.";
                        report.findings.push_back(finding);

                        report.precedence[i].may_shadow_later_routes = true;
                        report.precedence[j].may_be_shadowed = true;
                    }

                    // Earlier route wins as finding
                    TqRouteAnalysisFinding earlier_finding;
                    earlier_finding.severity = TqRouteAnalysisSeverity::Info;
                    earlier_finding.kind = TqRouteAnalysisKind::EarlierRouteWins;
                    earlier_finding.route_id = r2.route_id;
                    earlier_finding.related_route_id = r1.route_id;
                    earlier_finding.field_path = "match_prefix";
                    earlier_finding.reason = "Route '" + r2.route_id +
                                             "' will never be reached because duplicate route '" +
                                             r1.route_id + "' is processed first";
                    earlier_finding.hint =
                        "Move the earlier route below or remove the duplicate configuration.";
                    report.findings.push_back(earlier_finding);
                }
                // Prefix shadowing checks
                else if (r1.match_kind == TqRouteMatchKind::Prefix &&
                         is_prefix_shadow(r1.match_prefix, r2.match_prefix)) {
                    TqRouteAnalysisFinding finding;
                    finding.severity = TqRouteAnalysisSeverity::Warning;
                    finding.field_path = "match_prefix";
                    finding.route_id = r2.route_id;
                    finding.related_route_id = r1.route_id;

                    if (r2.match_kind == TqRouteMatchKind::Exact) {
                        finding.kind = TqRouteAnalysisKind::PrefixShadowsExact;
                        finding.reason = "Exact route '" + r2.route_id + "' (" + r2.match_prefix +
                                         ") is shadowed by earlier prefix route '" + r1.route_id +
                                         "' (" + r1.match_prefix + ")";
                        finding.hint = "Move more specific exact route blocks before broad prefix "
                                       "matching blocks.";
                    } else {
                        finding.kind = TqRouteAnalysisKind::PrefixShadowsPrefix;
                        finding.reason = "Prefix route '" + r2.route_id + "' (" + r2.match_prefix +
                                         ") is shadowed by earlier prefix route '" + r1.route_id +
                                         "' (" + r1.match_prefix + ")";
                        finding.hint = "Move more specific longer prefix routes before shorter "
                                       "prefix matching routes.";
                    }
                    report.findings.push_back(finding);

                    report.precedence[i].may_shadow_later_routes = true;
                    report.precedence[j].may_be_shadowed = true;
                }
            }

            // Shared L2 Path Warnings (across any pair)
            if (r1.cache.enabled && r1.cache.l2.enabled && !r1.cache.l2.path.empty() &&
                r2.cache.enabled && r2.cache.l2.enabled && !r2.cache.l2.path.empty()) {
                if (r1.cache.l2.path == r2.cache.l2.path) {
                    TqRouteAnalysisFinding finding;
                    finding.severity = TqRouteAnalysisSeverity::Warning;
                    finding.kind = TqRouteAnalysisKind::SharedL2CachePath;
                    finding.route_id = r2.route_id;
                    finding.related_route_id = r1.route_id;
                    finding.field_path = "cache.l2.path";
                    finding.reason = "Shared L2 cache storage path '" + r2.cache.l2.path +
                                     "' detected across routes '" + r1.route_id + "' and '" +
                                     r2.route_id + "'";
                    finding.hint = "Configure unique storage folders for different routes to avoid "
                                   "RocksDB lock or cache contamination.";
                    report.findings.push_back(finding);
                }
            }
        }
    }

    // 3. Invalidation target analysis
    for (const auto& mutation_route : policy.routes) {
        if (!mutation_route.cache.invalidation.enabled) {
            continue;
        }

        for (const auto& target : mutation_route.cache.invalidation.targets) {
            // Find target route
            const TqRoutePolicy* target_route = nullptr;
            for (const auto& r : policy.routes) {
                if (r.route_id == target.route_id) {
                    target_route = &r;
                    break;
                }
            }

            if (target_route == nullptr) {
                TqRouteAnalysisFinding finding;
                finding.severity = TqRouteAnalysisSeverity::Warning;
                finding.kind = TqRouteAnalysisKind::UnknownInvalidationTarget;
                finding.route_id = mutation_route.route_id;
                finding.field_path = "cache.invalidation.targets";
                finding.reason = "Mutation route '" + mutation_route.route_id +
                                 "' targets unknown route '" + target.route_id + "'";
                finding.hint = "Ensure the target route_id is defined in the policy document.";
                report.findings.push_back(finding);
                continue;
            }

            if (target_route->allowed_method != TqHttpMethod::Get) {
                TqRouteAnalysisFinding finding;
                finding.severity = TqRouteAnalysisSeverity::Warning;
                finding.kind = TqRouteAnalysisKind::InvalidInvalidationTargetMethod;
                finding.route_id = mutation_route.route_id;
                finding.related_route_id = target_route->route_id;
                finding.field_path = "cache.invalidation.targets";
                finding.reason = "Invalidation target '" + target_route->route_id +
                                 "' is not a GET route (method=" +
                                 method_to_string(target_route->allowed_method) + ")";
                finding.hint = "Mutation invalidation only supports clearing GET route caches.";
                report.findings.push_back(finding);
            }

            if (!target_route->cache.enabled ||
                target_route->cache.behavior != TqCacheBehavior::Store) {
                TqRouteAnalysisFinding finding;
                finding.severity = TqRouteAnalysisSeverity::Warning;
                finding.kind = TqRouteAnalysisKind::InvalidInvalidationTargetCachePolicy;
                finding.route_id = mutation_route.route_id;
                finding.related_route_id = target_route->route_id;
                finding.field_path = "cache.invalidation.targets";
                finding.reason = "Invalidation target '" + target_route->route_id +
                                 "' does not have cache.behavior=Store";
                finding.hint =
                    "The target route must have cache.behavior=Store to benefit from invalidation.";
                report.findings.push_back(finding);
            }

            if (target.strategy != TqCacheInvalidationStrategy::RouteEpoch) {
                TqRouteAnalysisFinding finding;
                finding.severity = TqRouteAnalysisSeverity::Warning;
                finding.kind = TqRouteAnalysisKind::UnsupportedInvalidationStrategy;
                finding.route_id = mutation_route.route_id;
                finding.field_path = "cache.invalidation.targets";
                finding.reason = "Unsupported invalidation strategy '" +
                                 strategy_to_string(target.strategy) + "' for target '" +
                                 target.route_id + "'";
                finding.hint = "Currently only 'RouteEpoch' strategy is supported.";
                report.findings.push_back(finding);
            }
        }
    }

    return report;
}

} // namespace bytetaper::taperquery

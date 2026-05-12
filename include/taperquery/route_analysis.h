// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_ROUTE_ANALYSIS_H
#define BYTETAPER_TAPERQUERY_ROUTE_ANALYSIS_H

#include "taperquery/policy_ir.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

enum class TqRouteAnalysisSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

enum class TqRouteAnalysisKind : std::uint8_t {
    DuplicateRouteId,
    DuplicateExactMatch,
    EquivalentPrefixMatch,
    PrefixShadowsPrefix,
    PrefixShadowsExact,
    EarlierRouteWins,
    SharedL2CachePath,
};

struct TqRouteAnalysisFinding {
    TqRouteAnalysisSeverity severity = TqRouteAnalysisSeverity::Warning;
    TqRouteAnalysisKind kind = TqRouteAnalysisKind::PrefixShadowsPrefix;
    std::string route_id;
    std::string related_route_id;
    std::string field_path;
    std::string reason;
    std::string hint;
};

struct TqRoutePrecedenceEntry {
    std::string route_id;
    std::string method;
    std::string match_kind;
    std::string path;
    std::uint32_t declaration_index = 0;
    std::uint32_t specificity_score = 0;
    bool may_shadow_later_routes = false;
    bool may_be_shadowed = false;
};

struct TqRouteAnalysisReport {
    bool ok = true;
    std::vector<TqRoutePrecedenceEntry> precedence;
    std::vector<TqRouteAnalysisFinding> findings;
};

// Compute a deterministic specificity score for a route policy
std::uint32_t compute_route_specificity_score(const TqRoutePolicy& route);

// Perform dedicated precedence and conflict analysis on a policy document
TqRouteAnalysisReport analyze_taperquery_route_precedence(const TqPolicyDocument& policy);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_ROUTE_ANALYSIS_H

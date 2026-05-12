// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_VALIDATOR_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_VALIDATOR_H

#include "taperquery/policy_ir.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::taperquery {

enum class TqPolicyValidationSeverity : std::uint8_t {
    Warning,
    Error,
};

enum class TqPolicyValidationSubsystem : std::uint8_t {
    Document,
    Version,
    Route,
    Match,
    Mutation,
    FieldFilter,
    Cache,
    Pagination,
    Compression,
    Coalescing,
    CrossFeature,
    CrossRoute,
};

struct TqPolicyValidationIssue {
    TqPolicyValidationSeverity severity = TqPolicyValidationSeverity::Error;
    TqPolicyValidationSubsystem subsystem = TqPolicyValidationSubsystem::Document;
    std::string route_id;
    std::string field_path;
    std::string code;
    std::string reason;
    std::string hint;
};

struct TqPolicyValidationOptions {
    bool collect_all = true;
    bool include_warnings = true;
    bool strict_production = true;
};

struct TqPolicyValidationResult {
    bool ok = true;
    std::vector<TqPolicyValidationIssue> issues;
};

// Validates a full parsed policy document (cross-route checks + version validation).
TqPolicyValidationResult
validate_taperquery_policy_ir(const TqPolicyDocument& policy,
                              const TqPolicyValidationOptions& options = {});

// Validates a single RoutePolicy semantically.
TqPolicyValidationResult
validate_taperquery_route_policy_ir(const TqRoutePolicy& route,
                                    const TqPolicyValidationOptions& options = {});

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_VALIDATOR_H

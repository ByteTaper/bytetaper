// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "policy/route_policy.h"
#include "policy/yaml_loader.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::policy {

enum class PolicyValidationSeverity : std::uint8_t {
    Error = 0,
    Warning = 1,
};

enum class PolicyValidationSubsystem : std::uint8_t {
    Route = 0,
    Cache = 1,
    Compression = 2,
    Pagination = 3,
    Coalescing = 4,
    CrossRoute = 5,
};

struct PolicyValidationIssue {
    PolicyValidationSeverity severity = PolicyValidationSeverity::Error;
    PolicyValidationSubsystem subsystem = PolicyValidationSubsystem::Route;
    const char* route_id = nullptr;
    const char* field = nullptr;
    const char* reason = nullptr;
};

struct PolicyValidationOptions {
    bool collect_all = false;
    bool include_warnings = true;
};

static constexpr std::size_t kMaxPolicyValidationIssues = 256;

struct PolicyValidationResult {
    bool ok = true;
    std::size_t issue_count = 0;
    PolicyValidationIssue issues[kMaxPolicyValidationIssues] = {};
};

// Validates a single RoutePolicy semantically.
// Returns false if any Error is added.
bool validate_route_policy_semantic(const RoutePolicy& route, PolicyValidationResult* result,
                                    const PolicyValidationOptions& options);

// Validates a full parsed policy file (cross-route checks).
// Returns false if any Error is found in any route or at the file level.
bool validate_policy_file_semantic(const PolicyFileResult& policy_file,
                                   PolicyValidationResult* result,
                                   const PolicyValidationOptions& options);

} // namespace bytetaper::policy

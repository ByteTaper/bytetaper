// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "policy/policy_reporter.h"
#include "policy/policy_semantic_validator.h"
#include "policy/yaml_loader.h"

#include <cstdio>

namespace {

static const char* subsystem_to_string(bytetaper::policy::PolicyValidationSubsystem sub) {
    switch (sub) {
    case bytetaper::policy::PolicyValidationSubsystem::Route:
        return "route";
    case bytetaper::policy::PolicyValidationSubsystem::Cache:
        return "cache";
    case bytetaper::policy::PolicyValidationSubsystem::Compression:
        return "compression";
    case bytetaper::policy::PolicyValidationSubsystem::Pagination:
        return "pagination";
    case bytetaper::policy::PolicyValidationSubsystem::Coalescing:
        return "coalescing";
    case bytetaper::policy::PolicyValidationSubsystem::CrossRoute:
        return "cross-route";
    }
    return "unknown";
}

} // namespace

/**
 * bytetaper-validate-policy CLI tool.
 *
 * Usage: bytetaper-validate-policy <path-to-policy.yaml>
 *
 * Exit codes:
 * 0 - Success: all routes valid
 * 1 - Usage error: wrong argument count
 * 2 - Load/Parse error: YAML file invalid or missing
 * 3 - Validation error: one or more routes failed validation
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bytetaper-validate-policy <policy.yaml>\n");
        return 1;
    }

    bytetaper::policy::PolicyFileResult result{};
    if (!bytetaper::policy::load_policy_from_file(argv[1], &result)) {
        std::fprintf(stderr, "error: %s\n", result.error ? result.error : "unknown");
        return 2;
    }

    bytetaper::policy::PolicyValidationResult validation{};
    bytetaper::policy::PolicyValidationOptions options{};
    options.collect_all = false; // fail-fast
    options.include_warnings = true;

    bytetaper::policy::validate_policy_file_semantic(result, &validation, options);

    for (std::size_t i = 0; i < validation.issue_count; ++i) {
        const auto& issue = validation.issues[i];
        const char* r_id = issue.route_id ? issue.route_id : "(null)";
        const char* sub_str = subsystem_to_string(issue.subsystem);
        const char* field_str = issue.field ? issue.field : "(null)";
        const char* reason_str = issue.reason ? issue.reason : "unknown";
        if (issue.severity == bytetaper::policy::PolicyValidationSeverity::Warning) {
            std::fprintf(stderr, "warning route '%s': [%s] field '%s': %s\n", r_id, sub_str,
                         field_str, reason_str);
        } else {
            std::fprintf(stderr, "invalid route '%s': [%s] field '%s': %s\n", r_id, sub_str,
                         field_str, reason_str);
        }
    }

    if (!validation.ok) {
        return 3;
    }

    bytetaper::policy::print_route_policy_report(stdout, result.policies, result.count);
    return 0;
}

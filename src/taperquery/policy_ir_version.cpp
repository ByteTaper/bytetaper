// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "taperquery/policy_ir_version.h"

#include "taperquery/policy_ir.h"

namespace bytetaper::taperquery {

bool is_supported_policy_ir_version(const std::string& version) {
    return version == kCurrentPolicyIrVersion;
}

bool is_supported_identity_version(const std::string& version) {
    return version == kCurrentPolicyIdentityVersion;
}

bool is_supported_source_schema_version(const std::string& version) {
    return version == "tq/v1" || version == "yaml/v1";
}

TqVersionValidationResult validate_policy_version_info(const TqPolicyVersionInfo& info) {
    TqVersionValidationResult result;
    if (!is_supported_source_schema_version(info.source_schema_version)) {
        result.ok = false;
        TqVersionValidationIssue issue;
        issue.field = "version.source_schema_version";
        issue.value = info.source_schema_version;
        issue.reason = "unsupported source schema version";
        result.issues.push_back(issue);
    }
    if (!is_supported_policy_ir_version(info.policy_ir_version)) {
        result.ok = false;
        TqVersionValidationIssue issue;
        issue.field = "version.policy_ir_version";
        issue.value = info.policy_ir_version;
        issue.reason = "unsupported policy IR version";
        result.issues.push_back(issue);
    }
    if (!is_supported_identity_version(info.identity_version)) {
        result.ok = false;
        TqVersionValidationIssue issue;
        issue.field = "version.identity_version";
        issue.value = info.identity_version;
        issue.reason = "unsupported policy identity version";
        result.issues.push_back(issue);
    }
    return result;
}

} // namespace bytetaper::taperquery

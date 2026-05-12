// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_VERSION_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_VERSION_H

#include <string>
#include <string_view>
#include <vector>

namespace bytetaper::taperquery {

constexpr std::string_view kCurrentPolicyIdentityVersion = "policy-identity/v2";
constexpr std::string_view kCurrentPolicyIrVersion = "tq-ir/v1";

struct TqVersionValidationIssue {
    std::string field;
    std::string value;
    std::string reason;
};

struct TqVersionValidationResult {
    bool ok = true;
    std::vector<TqVersionValidationIssue> issues;
};

struct TqPolicyVersionInfo;

bool is_supported_policy_ir_version(const std::string& version);
bool is_supported_identity_version(const std::string& version);
bool is_supported_source_schema_version(const std::string& version);

TqVersionValidationResult validate_policy_version_info(const TqPolicyVersionInfo& info);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_VERSION_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_POLICY_MISMATCH_CLASSIFIER_H
#define BYTETAPER_RUNTIME_POLICY_POLICY_MISMATCH_CLASSIFIER_H

#include "runtime_policy/runtime_policy_status.h"

#include <cstdint>
#include <string>

namespace bytetaper::runtime_policy {

struct PolicyMismatchInput {
    std::uint64_t committed_generation = 0;
    std::string committed_policy_id;
    std::string committed_canonical_hash;

    bool local_file_exists = false;
    bool local_file_parseable = false;
    std::uint64_t local_generation = 0;
    std::string local_policy_id;
    std::string local_canonical_hash;
    bool metadata_present = false;
    bool metadata_valid = false;

    bool version_record_exists = false;
    bool version_hash_matches = false;
    bool committed_yaml_hash_matches = false;

    // When false, local mirror presence/mismatch does not block store-authoritative activation.
    bool require_local_mirror = true;
};

RuntimePolicyHealth classify_mismatch(const PolicyMismatchInput& input);

std::string strip_sha256_prefix(const std::string& hash);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_POLICY_MISMATCH_CLASSIFIER_H

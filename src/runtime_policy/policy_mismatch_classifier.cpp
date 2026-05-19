// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/policy_mismatch_classifier.h"

namespace bytetaper::runtime_policy {

std::string strip_sha256_prefix(const std::string& hash) {
    if (hash.size() >= 7 && hash.rfind("sha256:", 0) == 0) {
        return hash.substr(7);
    }
    return hash;
}

RuntimePolicyHealth classify_mismatch(const PolicyMismatchInput& input) {
    if (input.version_record_exists && !input.version_hash_matches) {
        return RuntimePolicyHealth::InactiveActivePointerMissingTarget;
    }
    if (input.version_record_exists && !input.committed_yaml_hash_matches) {
        return RuntimePolicyHealth::InactiveActivePointerMissingTarget;
    }

    if (!input.require_local_mirror) {
        if (input.version_record_exists && input.version_hash_matches &&
            input.committed_yaml_hash_matches) {
            return RuntimePolicyHealth::Active;
        }
        return RuntimePolicyHealth::InactiveActivePointerMissingTarget;
    }

    if (input.local_file_exists && !input.local_file_parseable) {
        return RuntimePolicyHealth::InactiveUnparseableLocal;
    }
    if (!input.local_file_exists) {
        return RuntimePolicyHealth::InactiveMissingLocal;
    }
    if (!input.metadata_present) {
        return RuntimePolicyHealth::InactiveMetadataMissing;
    }
    if (!input.metadata_valid) {
        return RuntimePolicyHealth::InactiveMetadataInvalid;
    }

    if (input.local_generation < input.committed_generation) {
        return RuntimePolicyHealth::InactiveStaleLocal;
    }
    if (input.local_generation > input.committed_generation) {
        return RuntimePolicyHealth::InactiveDivergedLocal;
    }

    const std::string committed_hash = strip_sha256_prefix(input.committed_canonical_hash);
    const std::string local_hash = strip_sha256_prefix(input.local_canonical_hash);
    if (input.local_generation == input.committed_generation && committed_hash != local_hash) {
        return RuntimePolicyHealth::InactiveCorruptedLocal;
    }

    if (input.local_generation == input.committed_generation &&
        input.local_policy_id == input.committed_policy_id && committed_hash == local_hash) {
        return RuntimePolicyHealth::Active;
    }

    return RuntimePolicyHealth::InactiveCorruptedLocal;
}

} // namespace bytetaper::runtime_policy

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_GENERATION_COMMIT_H
#define BYTETAPER_CONTROL_PLANE_POLICY_GENERATION_COMMIT_H

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/policy_state_store.h"

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

struct PolicyGenerationCommitInput {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    ExpectedActivePolicy expected_active{};
    std::string canonical_yaml;
    std::string source_type;
    std::string apply_id;
    std::string operator_id;
    std::string request_id;
    std::uint64_t candidate_generation_override = 0;
    bool preserve_canonical_yaml = false;
    std::string canonical_hash_override;
};

struct PolicyGenerationCommitResult {
    bool ok = false;
    std::string error_code;
    std::string error_message;
    std::uint64_t new_generation = 0;
    std::string new_policy_id;
    std::string new_canonical_hash;
    ActivePolicyPointer promoted_pointer{};
};

PolicyGenerationCommitResult
commit_policy_generation(PolicyStateStore* store, const PolicyGenerationCommitInput& input,
                         const PolicyAuditRecord* success_audit = nullptr);

std::uint64_t resolve_adopt_candidate_generation(PolicyStateStore* store,
                                                 const PolicyResourceKey& key,
                                                 std::uint64_t active_generation,
                                                 std::uint64_t local_generation_hint);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_GENERATION_COMMIT_H

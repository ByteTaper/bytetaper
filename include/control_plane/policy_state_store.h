// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_STATE_STORE_H
#define BYTETAPER_CONTROL_PLANE_POLICY_STATE_STORE_H

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/policy_state_result.h"

namespace bytetaper::control_plane {

struct CommitPolicyGenerationWithAuditParams {
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    PolicyVersionRecord version{};
    std::string canonical_yaml;
    ExpectedActivePolicy expected_active{};
    ActivePolicyPointer next_pointer{};
    PolicyAuditRecord audit{};
};

struct CommitPolicyGenerationWithAuditResult {
    bool ok = false;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

class PolicyStateStore {
public:
    virtual ~PolicyStateStore() = default;

    virtual StorePolicyVersionResult store_policy_version(const PolicyResourceKey& key,
                                                          const PolicyVersionRecord& version,
                                                          const std::string& canonical_yaml) = 0;

    virtual LoadPolicyVersionResult load_policy_version(const PolicyResourceKey& key,
                                                        std::uint64_t generation) = 0;

    virtual LoadActivePointerResult load_active_pointer(const PolicyResourceKey& key) = 0;

    virtual PromoteActiveResult compare_and_promote_active(const PolicyResourceKey& key,
                                                           const ExpectedActivePolicy& expected,
                                                           const ActivePolicyPointer& next) = 0;

    virtual AppendAuditResult append_audit_record(const PolicyResourceKey& key,
                                                  const PolicyAuditRecord& record) = 0;

    virtual CommitPolicyGenerationWithAuditResult
    commit_policy_generation_with_audit(const CommitPolicyGenerationWithAuditParams& params) = 0;

    virtual StorePolicyUpdateJobResult
    store_policy_update_job(const PolicyResourceKey& key, const PolicyUpdateJobRecord& job) = 0;

    virtual LoadPolicyUpdateJobResult load_policy_update_job(const PolicyResourceKey& key,
                                                             const std::string& job_id) = 0;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_STATE_STORE_H

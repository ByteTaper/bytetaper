// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_APPLY_TRANSACTION_H
#define BYTETAPER_CONTROL_PLANE_POLICY_APPLY_TRANSACTION_H

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"
#include "control_plane/policy_update_job.h"
#include "runtime/policy_snapshot.h"
#include "taperquery/tq_apply_service.h"

#include <cstdint>
#include <functional>
#include <string>

namespace bytetaper::control_plane {

struct PolicyApplyTransactionConfig {
    PolicyStateStore* policy_state_store = nullptr;
    taperquery::TqApplyService* tq_apply_service = nullptr;
    runtime::RuntimePolicyStore* scratch_policy_store = nullptr;
    PolicyResourceKey resource_key = PolicyResourceKey::default_runtime();
    std::function<void(const PolicyUpdateJob&)> on_state_change;
};

enum class PolicyApplyStage : std::uint8_t {
    LoadActive,
    ValidateBase,
    LoadBasePolicy,
    BuildCandidate,
    Canonicalize,
    StoreVersion,
    CompareAndPromote,
    RecordResult,
};

struct PolicyApplyTransactionResult {
    bool ok = false;
    PolicyUpdateJobState final_state = PolicyUpdateJobState::Failed;
    PolicyApplyStage failure_stage = PolicyApplyStage::LoadActive;
    std::string error_code;
    std::string error_message;
    std::uint64_t candidate_generation = 0;
    std::string candidate_policy_id;
    std::string candidate_canonical_hash;
    bool idempotent = false;
};

class PolicyApplyTransaction {
public:
    explicit PolicyApplyTransaction(PolicyApplyTransactionConfig config);

    PolicyApplyTransactionResult execute(PolicyUpdateJob& job);

private:
    PolicyApplyTransactionResult make_failure(PolicyUpdateJob& job, PolicyApplyStage stage,
                                              const std::string& code, const std::string& message,
                                              std::uint64_t expected_generation = 0,
                                              std::uint64_t actual_generation = 0);

    void notify_state_change(const PolicyUpdateJob& job) const;

    PolicyApplyTransactionConfig config_;
};

const char* to_string(PolicyApplyStage stage);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_APPLY_TRANSACTION_H

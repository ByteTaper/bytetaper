// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_LIFECYCLE_EVENT_H
#define BYTETAPER_CONTROL_PLANE_POLICY_LIFECYCLE_EVENT_H

#include "control_plane/policy_state_record.h"

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

struct PolicyUpdateJob;

enum class PolicyLifecycleEventType : std::uint8_t {
    PolicyDryRunRequested,
    PolicyApplySubmitted,
    PolicyApplyQueued,
    PolicyApplyProcessing,
    PolicyApplyCandidateBuilt,
    PolicyVersionStored,
    PolicyActivePromoted,
    PolicyApplyCommitted,
    PolicyApplyFailed,
    PolicyActivationStarted,
    PolicyActivationSucceeded,
    PolicyActivationFailed,
    RuntimeConverged,
    RuntimeStale,
    RuntimeActivationFailed,
    PolicyMismatchDetected,
    ManualRepairRequested,
    ManualRepairCompleted,
    ManualAdoptRequested,
    ManualAdoptCompleted,
    PolicyRollbackRequested,
    PolicyRollbackCompleted,
};

const char* to_string(PolicyLifecycleEventType type);

struct PolicyLifecycleEvent {
    PolicyLifecycleEventType event_type = PolicyLifecycleEventType::PolicyApplySubmitted;
    std::string resource_key;
    std::string operator_id;
    std::string request_id;
    std::string apply_id;
    std::string job_id;
    std::string event_id;
    std::uint64_t before_generation = 0;
    std::uint64_t after_generation = 0;
    std::uint64_t target_generation = 0;
    std::string before_policy_id;
    std::string after_policy_id;
    std::string policy_id;
    std::string canonical_hash;
    std::string source_type;
    std::string operation;
    std::string stage;
    std::string status;
    std::string error_code;
    std::string message;
    std::uint64_t duration_ms = 0;
    std::uint64_t recorded_at_unix_epoch_ms = 0;
    bool manual_resolution_required = false;
    bool old_snapshot_still_active = false;
};

PolicyAuditRecord lifecycle_event_to_audit_record(const PolicyLifecycleEvent& event);

PolicyLifecycleEvent make_lifecycle_event_from_job(const PolicyUpdateJob& job,
                                                   PolicyLifecycleEventType type);

std::uint64_t policy_lifecycle_now_ms();

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_LIFECYCLE_EVENT_H

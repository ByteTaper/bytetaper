// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_lifecycle_event.h"

#include "control_plane/policy_update_job.h"

#include <chrono>

namespace bytetaper::control_plane {

namespace {

const char* kStatusSuccess = "success";
const char* kStatusFailure = "failure";

} // namespace

const char* to_string(PolicyLifecycleEventType type) {
    switch (type) {
    case PolicyLifecycleEventType::PolicyDryRunRequested:
        return "policy_dry_run_requested";
    case PolicyLifecycleEventType::PolicyApplySubmitted:
        return "policy_apply_submitted";
    case PolicyLifecycleEventType::PolicyApplyQueued:
        return "policy_apply_queued";
    case PolicyLifecycleEventType::PolicyApplyProcessing:
        return "policy_apply_processing";
    case PolicyLifecycleEventType::PolicyApplyCandidateBuilt:
        return "policy_apply_candidate_built";
    case PolicyLifecycleEventType::PolicyVersionStored:
        return "policy_version_stored";
    case PolicyLifecycleEventType::PolicyActivePromoted:
        return "policy_active_promoted";
    case PolicyLifecycleEventType::PolicyApplyCommitted:
        return "policy_apply_committed";
    case PolicyLifecycleEventType::PolicyApplyFailed:
        return "policy_apply_failed";
    case PolicyLifecycleEventType::PolicyActivationStarted:
        return "policy_activation_started";
    case PolicyLifecycleEventType::PolicyActivationSucceeded:
        return "policy_activation_succeeded";
    case PolicyLifecycleEventType::PolicyActivationFailed:
        return "policy_activation_failed";
    case PolicyLifecycleEventType::RuntimeConverged:
        return "runtime_converged";
    case PolicyLifecycleEventType::RuntimeStale:
        return "runtime_stale";
    case PolicyLifecycleEventType::RuntimeActivationFailed:
        return "runtime_activation_failed";
    case PolicyLifecycleEventType::PolicyMismatchDetected:
        return "policy_mismatch_detected";
    case PolicyLifecycleEventType::ManualRepairRequested:
        return "manual_repair_requested";
    case PolicyLifecycleEventType::ManualRepairCompleted:
        return "manual_repair_completed";
    case PolicyLifecycleEventType::ManualAdoptRequested:
        return "manual_adopt_requested";
    case PolicyLifecycleEventType::ManualAdoptCompleted:
        return "manual_adopt_completed";
    case PolicyLifecycleEventType::PolicyRollbackRequested:
        return "policy_rollback_requested";
    case PolicyLifecycleEventType::PolicyRollbackCompleted:
        return "policy_rollback_completed";
    }
    return "unknown";
}

std::uint64_t policy_lifecycle_now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

PolicyLifecycleEvent make_lifecycle_event_from_job(const PolicyUpdateJob& job,
                                                   PolicyLifecycleEventType type) {
    PolicyLifecycleEvent event{};
    event.event_type = type;
    event.resource_key = job.resource_key;
    event.job_id = job.job_id;
    event.request_id = job.request_id;
    event.operator_id = job.operator_id;
    event.apply_id = job.request_id.empty() ? job.job_id : job.request_id;
    event.source_type = job.source_type;
    event.before_generation = job.expected_base_generation;
    event.before_policy_id = job.expected_base_policy_id;
    event.after_generation = job.candidate_generation;
    event.after_policy_id = job.candidate_policy_id;
    event.policy_id = job.candidate_policy_id;
    event.canonical_hash = job.candidate_canonical_hash;
    event.stage = job.failure.stage;
    event.error_code = job.failure.code;
    event.message = job.failure.message;
    event.recorded_at_unix_epoch_ms = policy_lifecycle_now_ms();
    return event;
}

PolicyAuditRecord lifecycle_event_to_audit_record(const PolicyLifecycleEvent& event) {
    PolicyAuditRecord record{};
    record.record_version = 3;
    record.record_type = "PolicyAuditRecord";
    record.apply_id = event.apply_id.empty() ? event.request_id : event.apply_id;
    record.event_id = event.event_id.empty() ? record.apply_id : event.event_id;
    record.resource_key = event.resource_key;
    record.generation =
        event.after_generation != 0 ? event.after_generation : event.before_generation;
    record.policy_id = event.after_policy_id.empty() ? event.policy_id : event.after_policy_id;
    record.source_type = event.source_type;
    record.operator_id = event.operator_id;
    record.request_id = event.request_id;
    record.recorded_at_unix_epoch_ms = event.recorded_at_unix_epoch_ms != 0
                                           ? event.recorded_at_unix_epoch_ms
                                           : policy_lifecycle_now_ms();
    record.operation = event.operation.empty() ? to_string(event.event_type) : event.operation;
    record.before_generation = event.before_generation;
    record.after_generation = event.after_generation;
    record.target_generation = event.target_generation;
    record.event_type = to_string(event.event_type);
    record.job_id = event.job_id;
    record.before_policy_id = event.before_policy_id;
    record.after_policy_id = event.after_policy_id;
    record.canonical_hash = event.canonical_hash;
    record.lifecycle_status = event.status;
    record.failure_code = event.error_code;
    record.failure_stage = event.stage;
    record.message = event.message;

    const bool success = event.status == kStatusSuccess || event.status.empty();
    record.result = success ? kStatusSuccess : kStatusFailure;
    record.failure_reason = success ? "" : event.message;
    return record;
}

} // namespace bytetaper::control_plane

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_update_job_record.h"

namespace bytetaper::control_plane {

PolicyUpdateJobRecord to_job_record(const PolicyUpdateJob& job) {
    PolicyUpdateJobRecord record;
    record.job_id = job.job_id;
    record.resource_key = job.resource_key;
    record.logical_shard_id = job.logical_shard_id;
    record.state = to_string(job.state);
    record.source_type = job.source_type;
    record.operator_id = job.operator_id;
    record.request_id = job.request_id;
    record.expected_base_generation = job.expected_base_generation;
    record.expected_base_policy_id = job.expected_base_policy_id;
    record.candidate_generation = job.candidate_generation;
    record.candidate_policy_id = job.candidate_policy_id;
    record.candidate_canonical_hash = job.candidate_canonical_hash;
    record.submitted_at_unix_epoch_ms = job.submitted_at_unix_epoch_ms;
    record.updated_at_unix_epoch_ms = job.updated_at_unix_epoch_ms;
    record.failure.stage = job.failure.stage;
    record.failure.code = job.failure.code;
    record.failure.message = job.failure.message;
    record.failure.expected_generation = job.failure.expected_generation;
    record.failure.actual_generation = job.failure.actual_generation;
    record.activation_status = job.activation_status;
    record.activation_stage = job.activation_stage;
    record.activation_message = job.activation_message;
    return record;
}

void apply_job_record_to_job(const PolicyUpdateJobRecord& record, PolicyUpdateJob* job) {
    if (job == nullptr) {
        return;
    }

    job->job_id = record.job_id;
    job->resource_key = record.resource_key;
    job->logical_shard_id = record.logical_shard_id;
    job->state = parse_policy_update_job_state(record.state);
    job->source_type = record.source_type;
    job->operator_id = record.operator_id;
    job->request_id = record.request_id;
    job->expected_base_generation = record.expected_base_generation;
    job->expected_base_policy_id = record.expected_base_policy_id;
    job->candidate_generation = record.candidate_generation;
    job->candidate_policy_id = record.candidate_policy_id;
    job->candidate_canonical_hash = record.candidate_canonical_hash;
    job->submitted_at_unix_epoch_ms = record.submitted_at_unix_epoch_ms;
    job->updated_at_unix_epoch_ms = record.updated_at_unix_epoch_ms;
    job->failure.stage = record.failure.stage;
    job->failure.code = record.failure.code;
    job->failure.message = record.failure.message;
    job->failure.expected_generation = record.failure.expected_generation;
    job->failure.actual_generation = record.failure.actual_generation;
    job->activation_status = record.activation_status;
    job->activation_stage = record.activation_stage;
    job->activation_message = record.activation_message;
}

PolicyUpdateJobState parse_policy_update_job_state(const std::string& state) {
    if (state == "submitted" || state == "Submitted") {
        return PolicyUpdateJobState::Submitted;
    }
    if (state == "queued" || state == "Queued") {
        return PolicyUpdateJobState::Queued;
    }
    if (state == "processing" || state == "Processing") {
        return PolicyUpdateJobState::Processing;
    }
    if (state == "candidate_built" || state == "CandidateBuilt") {
        return PolicyUpdateJobState::CandidateBuilt;
    }
    if (state == "version_stored" || state == "VersionStored") {
        return PolicyUpdateJobState::VersionStored;
    }
    if (state == "active_promoted" || state == "ActivePromoted") {
        return PolicyUpdateJobState::ActivePromoted;
    }
    if (state == "committed" || state == "Committed") {
        return PolicyUpdateJobState::Committed;
    }
    if (state == "failed" || state == "Failed") {
        return PolicyUpdateJobState::Failed;
    }
    if (state == "cancelled" || state == "Cancelled") {
        return PolicyUpdateJobState::Cancelled;
    }
    return PolicyUpdateJobState::Submitted;
}

} // namespace bytetaper::control_plane

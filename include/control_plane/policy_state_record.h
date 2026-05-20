// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_STATE_RECORD_H
#define BYTETAPER_CONTROL_PLANE_POLICY_STATE_RECORD_H

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

struct ActivePolicyPointer {
    std::string record_type = "ActivePolicyPointer";
    std::uint32_t record_version = 1;
    std::string resource_key;
    std::uint64_t generation = 0;
    std::string policy_id;
    std::string canonical_hash;
    std::uint32_t schema_version = 0;
    std::string api_version;
    std::string kind;
    std::string version_key;
    std::string yaml_key;
    std::string source_type;
    std::string apply_id;
    std::uint64_t previous_generation = 0;
    std::string previous_policy_id;
    std::uint64_t committed_at_unix_epoch_ms = 0;
};

struct PolicyVersionRecord {
    std::string record_type = "PolicyVersionRecord";
    std::uint32_t record_version = 1;
    std::string resource_key;
    std::uint64_t generation = 0;
    std::string policy_id;
    std::string canonical_hash;
    std::uint32_t schema_version = 0;
    std::string api_version;
    std::string kind;
    std::string yaml_key;
    std::string source_type;
    std::string apply_id;
    std::uint64_t previous_generation = 0;
    std::string previous_policy_id;
    std::uint64_t created_at_unix_epoch_ms = 0;
};

struct PolicyAuditRecord {
    std::string record_type = "PolicyAuditRecord";
    std::uint32_t record_version = 2;
    std::string apply_id;
    std::string resource_key;
    std::uint64_t generation = 0;
    std::string policy_id;
    std::string source_type;
    std::string operator_id;
    std::string request_id;
    std::uint64_t recorded_at_unix_epoch_ms = 0;
    std::string operation;
    std::uint64_t before_generation = 0;
    std::uint64_t after_generation = 0;
    std::uint64_t target_generation = 0;
    std::string result;
    std::string failure_reason;
};

struct PolicyUpdateJobFailureRecord {
    std::string stage;
    std::string code;
    std::string message;
    std::uint64_t expected_generation = 0;
    std::uint64_t actual_generation = 0;
};

struct PolicyUpdateJobRecord {
    std::string record_type = "PolicyUpdateJobRecord";
    std::uint32_t record_version = 1;
    std::string job_id;
    std::string resource_key;
    std::uint32_t logical_shard_id = 0;
    std::string state;
    std::string source_type;
    std::string operator_id;
    std::string request_id;
    std::uint64_t expected_base_generation = 0;
    std::string expected_base_policy_id;
    std::uint64_t candidate_generation = 0;
    std::string candidate_policy_id;
    std::string candidate_canonical_hash;
    std::uint64_t submitted_at_unix_epoch_ms = 0;
    std::uint64_t updated_at_unix_epoch_ms = 0;
    PolicyUpdateJobFailureRecord failure{};
    std::string activation_status;
    std::string activation_stage;
    std::string activation_message;
};

struct ExpectedActivePolicy {
    std::uint64_t generation = 0;
    std::string policy_id;
};

std::string serialize_active_pointer(const ActivePolicyPointer& pointer);
bool deserialize_active_pointer(const std::string& json, ActivePolicyPointer* out);

std::string serialize_version_record(const PolicyVersionRecord& record);
bool deserialize_version_record(const std::string& json, PolicyVersionRecord* out);

std::string serialize_audit_record(const PolicyAuditRecord& record);
bool deserialize_audit_record(const std::string& json, PolicyAuditRecord* out);

std::string serialize_policy_update_job_record(const PolicyUpdateJobRecord& record);
bool deserialize_policy_update_job_record(const std::string& json, PolicyUpdateJobRecord* out);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_STATE_RECORD_H

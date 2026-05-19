// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_state_record.h"

#include <cctype>
#include <cstdio>

namespace bytetaper::control_plane {

namespace {

std::string escape_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else if (static_cast<unsigned char>(c) < 32) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<int>(c));
            out += hex;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string get_json_string_field(const std::string& json, const std::string& key) {
    const std::string search_key = "\"" + key + "\"";
    const std::size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return "";
    }

    const std::size_t colon_pos = json.find(':', pos + search_key.size());
    if (colon_pos == std::string::npos) {
        return "";
    }

    const std::size_t quote_start = json.find('"', colon_pos);
    if (quote_start == std::string::npos) {
        return "";
    }

    std::size_t quote_end = quote_start + 1;
    std::string val;
    while (quote_end < json.size()) {
        if (json[quote_end] == '"') {
            break;
        }
        if (json[quote_end] == '\\' && quote_end + 1 < json.size()) {
            const char next = json[quote_end + 1];
            if (next == '"') {
                val += '"';
            } else if (next == '\\') {
                val += '\\';
            } else if (next == 'n') {
                val += '\n';
            } else if (next == 't') {
                val += '\t';
            } else if (next == 'r') {
                val += '\r';
            } else {
                val += next;
            }
            quote_end += 2;
        } else {
            val += json[quote_end];
            quote_end++;
        }
    }
    return val;
}

std::uint64_t get_json_uint64_field(const std::string& json, const std::string& key) {
    const std::string search_key = "\"" + key + "\"";
    const std::size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return 0;
    }

    const std::size_t colon_pos = json.find(':', pos + search_key.size());
    if (colon_pos == std::string::npos) {
        return 0;
    }

    std::size_t val_start = colon_pos + 1;
    while (val_start < json.size() &&
           (std::isspace(static_cast<unsigned char>(json[val_start])) || json[val_start] == '"')) {
        val_start++;
    }
    std::size_t val_end = val_start;
    while (val_end < json.size() && std::isdigit(static_cast<unsigned char>(json[val_end]))) {
        val_end++;
    }
    if (val_end == val_start) {
        return 0;
    }

    return std::stoull(json.substr(val_start, val_end - val_start));
}

std::uint32_t get_json_uint32_field(const std::string& json, const std::string& key) {
    return static_cast<std::uint32_t>(get_json_uint64_field(json, key));
}

bool json_field_is_null(const std::string& json, const std::string& key) {
    const std::string search_key = "\"" + key + "\"";
    const std::size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return false;
    }

    const std::size_t colon_pos = json.find(':', pos + search_key.size());
    if (colon_pos == std::string::npos) {
        return false;
    }

    std::size_t val_start = colon_pos + 1;
    while (val_start < json.size() && std::isspace(static_cast<unsigned char>(json[val_start]))) {
        val_start++;
    }
    return json.compare(val_start, 4, "null") == 0;
}

std::string extract_json_object(const std::string& json, const std::string& key) {
    const std::string search_key = "\"" + key + "\"";
    const std::size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return "";
    }

    const std::size_t colon_pos = json.find(':', pos + search_key.size());
    if (colon_pos == std::string::npos) {
        return "";
    }

    std::size_t obj_start = colon_pos + 1;
    while (obj_start < json.size() && std::isspace(static_cast<unsigned char>(json[obj_start]))) {
        obj_start++;
    }
    if (obj_start >= json.size() || json[obj_start] != '{') {
        return "";
    }

    int depth = 0;
    for (std::size_t i = obj_start; i < json.size(); ++i) {
        if (json[i] == '{') {
            depth++;
        } else if (json[i] == '}') {
            depth--;
            if (depth == 0) {
                return json.substr(obj_start, i - obj_start + 1);
            }
        }
    }
    return "";
}

bool policy_update_job_has_failure_details(const PolicyUpdateJobRecord& record) {
    if (record.state == "failed") {
        return true;
    }
    return !record.failure.stage.empty() || !record.failure.code.empty() ||
           !record.failure.message.empty() || record.failure.expected_generation != 0 ||
           record.failure.actual_generation != 0;
}

void deserialize_policy_update_job_failure(const std::string& json,
                                           PolicyUpdateJobFailureRecord* failure) {
    if (failure == nullptr) {
        return;
    }

    if (json_field_is_null(json, "failure")) {
        return;
    }

    const std::string failure_json = extract_json_object(json, "failure");
    if (!failure_json.empty()) {
        failure->stage = get_json_string_field(failure_json, "stage");
        failure->code = get_json_string_field(failure_json, "code");
        failure->message = get_json_string_field(failure_json, "message");
        failure->expected_generation = get_json_uint64_field(failure_json, "expectedGeneration");
        failure->actual_generation = get_json_uint64_field(failure_json, "actualGeneration");
        return;
    }

    failure->stage = get_json_string_field(json, "failureStage");
    failure->code = get_json_string_field(json, "failureCode");
    failure->message = get_json_string_field(json, "failureMessage");
    failure->expected_generation = get_json_uint64_field(json, "failureExpectedGeneration");
    failure->actual_generation = get_json_uint64_field(json, "failureActualGeneration");
}

} // namespace

std::string serialize_active_pointer(const ActivePolicyPointer& pointer) {
    std::string json = "{\n";
    json += "  \"recordType\": \"" + escape_json_string(pointer.record_type) + "\",\n";
    json += "  \"recordVersion\": " + std::to_string(pointer.record_version) + ",\n";
    json += "  \"resourceKey\": \"" + escape_json_string(pointer.resource_key) + "\",\n";
    json += "  \"generation\": " + std::to_string(pointer.generation) + ",\n";
    json += "  \"policyId\": \"" + escape_json_string(pointer.policy_id) + "\",\n";
    json += "  \"canonicalHash\": \"" + escape_json_string(pointer.canonical_hash) + "\",\n";
    json += "  \"schemaVersion\": " + std::to_string(pointer.schema_version) + ",\n";
    json += "  \"apiVersion\": \"" + escape_json_string(pointer.api_version) + "\",\n";
    json += "  \"kind\": \"" + escape_json_string(pointer.kind) + "\",\n";
    json += "  \"versionKey\": \"" + escape_json_string(pointer.version_key) + "\",\n";
    json += "  \"yamlKey\": \"" + escape_json_string(pointer.yaml_key) + "\",\n";
    json += "  \"sourceType\": \"" + escape_json_string(pointer.source_type) + "\",\n";
    json += "  \"applyId\": \"" + escape_json_string(pointer.apply_id) + "\",\n";
    json += "  \"previousGeneration\": " + std::to_string(pointer.previous_generation) + ",\n";
    json += "  \"previousPolicyId\": \"" + escape_json_string(pointer.previous_policy_id) + "\",\n";
    json += "  \"committedAtUnixEpochMs\": " + std::to_string(pointer.committed_at_unix_epoch_ms) +
            "\n";
    json += "}\n";
    return json;
}

bool deserialize_active_pointer(const std::string& json, ActivePolicyPointer* out) {
    if (out == nullptr) {
        return false;
    }

    ActivePolicyPointer pointer;
    pointer.record_type = get_json_string_field(json, "recordType");
    pointer.record_version = get_json_uint32_field(json, "recordVersion");
    pointer.resource_key = get_json_string_field(json, "resourceKey");
    pointer.generation = get_json_uint64_field(json, "generation");
    pointer.policy_id = get_json_string_field(json, "policyId");
    pointer.canonical_hash = get_json_string_field(json, "canonicalHash");
    pointer.schema_version = get_json_uint32_field(json, "schemaVersion");
    pointer.api_version = get_json_string_field(json, "apiVersion");
    pointer.kind = get_json_string_field(json, "kind");
    pointer.version_key = get_json_string_field(json, "versionKey");
    pointer.yaml_key = get_json_string_field(json, "yamlKey");
    pointer.source_type = get_json_string_field(json, "sourceType");
    pointer.apply_id = get_json_string_field(json, "applyId");
    pointer.previous_generation = get_json_uint64_field(json, "previousGeneration");
    pointer.previous_policy_id = get_json_string_field(json, "previousPolicyId");
    pointer.committed_at_unix_epoch_ms = get_json_uint64_field(json, "committedAtUnixEpochMs");

    if (pointer.record_type.empty() || pointer.generation == 0 || pointer.policy_id.empty()) {
        return false;
    }

    *out = std::move(pointer);
    return true;
}

std::string serialize_version_record(const PolicyVersionRecord& record) {
    std::string json = "{\n";
    json += "  \"recordType\": \"" + escape_json_string(record.record_type) + "\",\n";
    json += "  \"recordVersion\": " + std::to_string(record.record_version) + ",\n";
    json += "  \"resourceKey\": \"" + escape_json_string(record.resource_key) + "\",\n";
    json += "  \"generation\": " + std::to_string(record.generation) + ",\n";
    json += "  \"policyId\": \"" + escape_json_string(record.policy_id) + "\",\n";
    json += "  \"canonicalHash\": \"" + escape_json_string(record.canonical_hash) + "\",\n";
    json += "  \"schemaVersion\": " + std::to_string(record.schema_version) + ",\n";
    json += "  \"apiVersion\": \"" + escape_json_string(record.api_version) + "\",\n";
    json += "  \"kind\": \"" + escape_json_string(record.kind) + "\",\n";
    json += "  \"yamlKey\": \"" + escape_json_string(record.yaml_key) + "\",\n";
    json += "  \"sourceType\": \"" + escape_json_string(record.source_type) + "\",\n";
    json += "  \"applyId\": \"" + escape_json_string(record.apply_id) + "\",\n";
    json += "  \"previousGeneration\": " + std::to_string(record.previous_generation) + ",\n";
    json += "  \"previousPolicyId\": \"" + escape_json_string(record.previous_policy_id) + "\",\n";
    json += "  \"createdAtUnixEpochMs\": " + std::to_string(record.created_at_unix_epoch_ms) + "\n";
    json += "}\n";
    return json;
}

bool deserialize_version_record(const std::string& json, PolicyVersionRecord* out) {
    if (out == nullptr) {
        return false;
    }

    PolicyVersionRecord record;
    record.record_type = get_json_string_field(json, "recordType");
    record.record_version = get_json_uint32_field(json, "recordVersion");
    record.resource_key = get_json_string_field(json, "resourceKey");
    record.generation = get_json_uint64_field(json, "generation");
    record.policy_id = get_json_string_field(json, "policyId");
    record.canonical_hash = get_json_string_field(json, "canonicalHash");
    record.schema_version = get_json_uint32_field(json, "schemaVersion");
    record.api_version = get_json_string_field(json, "apiVersion");
    record.kind = get_json_string_field(json, "kind");
    record.yaml_key = get_json_string_field(json, "yamlKey");
    record.source_type = get_json_string_field(json, "sourceType");
    record.apply_id = get_json_string_field(json, "applyId");
    record.previous_generation = get_json_uint64_field(json, "previousGeneration");
    record.previous_policy_id = get_json_string_field(json, "previousPolicyId");
    record.created_at_unix_epoch_ms = get_json_uint64_field(json, "createdAtUnixEpochMs");

    if (record.record_type.empty() || record.generation == 0 || record.policy_id.empty()) {
        return false;
    }

    *out = std::move(record);
    return true;
}

std::string serialize_audit_record(const PolicyAuditRecord& record) {
    std::string json = "{\n";
    json += "  \"recordType\": \"" + escape_json_string(record.record_type) + "\",\n";
    json += "  \"recordVersion\": " + std::to_string(record.record_version) + ",\n";
    json += "  \"applyId\": \"" + escape_json_string(record.apply_id) + "\",\n";
    json += "  \"resourceKey\": \"" + escape_json_string(record.resource_key) + "\",\n";
    json += "  \"generation\": " + std::to_string(record.generation) + ",\n";
    json += "  \"policyId\": \"" + escape_json_string(record.policy_id) + "\",\n";
    json += "  \"sourceType\": \"" + escape_json_string(record.source_type) + "\",\n";
    json += "  \"operatorId\": \"" + escape_json_string(record.operator_id) + "\",\n";
    json += "  \"requestId\": \"" + escape_json_string(record.request_id) + "\",\n";
    json +=
        "  \"recordedAtUnixEpochMs\": " + std::to_string(record.recorded_at_unix_epoch_ms) + "\n";
    json += "}\n";
    return json;
}

bool deserialize_audit_record(const std::string& json, PolicyAuditRecord* out) {
    if (out == nullptr) {
        return false;
    }

    PolicyAuditRecord record;
    record.record_type = get_json_string_field(json, "recordType");
    record.record_version = get_json_uint32_field(json, "recordVersion");
    record.apply_id = get_json_string_field(json, "applyId");
    record.resource_key = get_json_string_field(json, "resourceKey");
    record.generation = get_json_uint64_field(json, "generation");
    record.policy_id = get_json_string_field(json, "policyId");
    record.source_type = get_json_string_field(json, "sourceType");
    record.operator_id = get_json_string_field(json, "operatorId");
    record.request_id = get_json_string_field(json, "requestId");
    record.recorded_at_unix_epoch_ms = get_json_uint64_field(json, "recordedAtUnixEpochMs");

    if (record.record_type.empty() || record.apply_id.empty()) {
        return false;
    }

    *out = std::move(record);
    return true;
}

std::string serialize_policy_update_job_record(const PolicyUpdateJobRecord& record) {
    std::string json = "{\n";
    json += "  \"recordType\": \"" + escape_json_string(record.record_type) + "\",\n";
    json += "  \"recordVersion\": " + std::to_string(record.record_version) + ",\n";
    json += "  \"jobId\": \"" + escape_json_string(record.job_id) + "\",\n";
    json += "  \"resourceKey\": \"" + escape_json_string(record.resource_key) + "\",\n";
    json += "  \"logicalShardId\": " + std::to_string(record.logical_shard_id) + ",\n";
    json += "  \"state\": \"" + escape_json_string(record.state) + "\",\n";
    json += "  \"sourceType\": \"" + escape_json_string(record.source_type) + "\",\n";
    json += "  \"operatorId\": \"" + escape_json_string(record.operator_id) + "\",\n";
    json += "  \"requestId\": \"" + escape_json_string(record.request_id) + "\",\n";
    json +=
        "  \"expectedBaseGeneration\": " + std::to_string(record.expected_base_generation) + ",\n";
    json += "  \"expectedBasePolicyId\": \"" + escape_json_string(record.expected_base_policy_id) +
            "\",\n";
    json += "  \"candidateGeneration\": " + std::to_string(record.candidate_generation) + ",\n";
    json +=
        "  \"candidatePolicyId\": \"" + escape_json_string(record.candidate_policy_id) + "\",\n";
    json += "  \"candidateCanonicalHash\": \"" +
            escape_json_string(record.candidate_canonical_hash) + "\",\n";
    json += "  \"submittedAtUnixEpochMs\": " + std::to_string(record.submitted_at_unix_epoch_ms) +
            ",\n";
    json +=
        "  \"updatedAtUnixEpochMs\": " + std::to_string(record.updated_at_unix_epoch_ms) + ",\n";
    json += "  \"activationStatus\": \"" + escape_json_string(record.activation_status) + "\",\n";
    json += "  \"activationStage\": \"" + escape_json_string(record.activation_stage) + "\",\n";
    json += "  \"activationMessage\": \"" + escape_json_string(record.activation_message) + "\",\n";
    if (!policy_update_job_has_failure_details(record)) {
        json += "  \"failure\": null\n";
    } else {
        json += "  \"failure\": {\n";
        json += "    \"stage\": \"" + escape_json_string(record.failure.stage) + "\",\n";
        json += "    \"code\": \"" + escape_json_string(record.failure.code) + "\",\n";
        json += "    \"message\": \"" + escape_json_string(record.failure.message) + "\",\n";
        json +=
            "    \"expectedGeneration\": " + std::to_string(record.failure.expected_generation) +
            ",\n";
        json +=
            "    \"actualGeneration\": " + std::to_string(record.failure.actual_generation) + "\n";
        json += "  }\n";
    }
    json += "}\n";
    return json;
}

bool deserialize_policy_update_job_record(const std::string& json, PolicyUpdateJobRecord* out) {
    if (out == nullptr) {
        return false;
    }

    PolicyUpdateJobRecord record;
    record.record_type = get_json_string_field(json, "recordType");
    record.record_version = get_json_uint32_field(json, "recordVersion");
    record.job_id = get_json_string_field(json, "jobId");
    record.resource_key = get_json_string_field(json, "resourceKey");
    record.logical_shard_id = get_json_uint32_field(json, "logicalShardId");
    record.state = get_json_string_field(json, "state");
    record.source_type = get_json_string_field(json, "sourceType");
    record.operator_id = get_json_string_field(json, "operatorId");
    record.request_id = get_json_string_field(json, "requestId");
    record.expected_base_generation = get_json_uint64_field(json, "expectedBaseGeneration");
    record.expected_base_policy_id = get_json_string_field(json, "expectedBasePolicyId");
    record.candidate_generation = get_json_uint64_field(json, "candidateGeneration");
    record.candidate_policy_id = get_json_string_field(json, "candidatePolicyId");
    record.candidate_canonical_hash = get_json_string_field(json, "candidateCanonicalHash");
    record.submitted_at_unix_epoch_ms = get_json_uint64_field(json, "submittedAtUnixEpochMs");
    record.updated_at_unix_epoch_ms = get_json_uint64_field(json, "updatedAtUnixEpochMs");
    record.activation_status = get_json_string_field(json, "activationStatus");
    record.activation_stage = get_json_string_field(json, "activationStage");
    record.activation_message = get_json_string_field(json, "activationMessage");
    deserialize_policy_update_job_failure(json, &record.failure);

    if (record.record_type.empty() || record.job_id.empty()) {
        return false;
    }

    *out = std::move(record);
    return true;
}

} // namespace bytetaper::control_plane

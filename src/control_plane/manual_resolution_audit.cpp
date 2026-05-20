// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/manual_resolution_audit.h"

#include <chrono>

namespace bytetaper::control_plane {

std::uint64_t manual_resolution_audit_now_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

PolicyLifecycleEvent make_manual_lifecycle_event(
    PolicyLifecycleEventType event_type, const PolicyResourceKey& key, const std::string& operation,
    const std::string& source_type, const std::string& operator_id, const std::string& request_id,
    const std::string& status, const std::string& message, const std::string& error_code,
    const std::string& failure_stage, std::uint64_t before_generation,
    std::uint64_t after_generation, std::uint64_t target_generation) {
    PolicyLifecycleEvent event{};
    event.event_type = event_type;
    event.resource_key = key.to_string();
    event.operation = operation;
    event.source_type = source_type;
    event.operator_id = operator_id;
    event.request_id = request_id;
    event.apply_id = request_id;
    event.event_id = request_id;
    event.status = status;
    event.message = message;
    event.error_code = error_code;
    event.stage = failure_stage;
    event.before_generation = before_generation;
    event.after_generation = after_generation;
    event.target_generation = target_generation;
    event.recorded_at_unix_epoch_ms = manual_resolution_audit_now_ms();
    return event;
}

PolicyAuditRecord make_manual_resolution_audit_record(const PolicyLifecycleEvent& event) {
    return lifecycle_event_to_audit_record(event);
}

PolicyAuditRecord make_manual_resolution_audit_record(
    PolicyLifecycleEventType event_type, const PolicyResourceKey& key, const std::string& operation,
    const std::string& source_type, const std::string& operator_id, const std::string& request_id,
    const std::string& status, const std::string& message, const std::string& error_code,
    const std::string& failure_stage, std::uint64_t before_generation,
    std::uint64_t after_generation, std::uint64_t target_generation) {
    return make_manual_resolution_audit_record(make_manual_lifecycle_event(
        event_type, key, operation, source_type, operator_id, request_id, status, message,
        error_code, failure_stage, before_generation, after_generation, target_generation));
}

} // namespace bytetaper::control_plane

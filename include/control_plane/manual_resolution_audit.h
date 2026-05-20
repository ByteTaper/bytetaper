// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_AUDIT_H
#define BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_AUDIT_H

#include "control_plane/policy_lifecycle_event.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

std::uint64_t manual_resolution_audit_now_ms();

PolicyLifecycleEvent make_manual_lifecycle_event(
    PolicyLifecycleEventType event_type, const PolicyResourceKey& key, const std::string& operation,
    const std::string& source_type, const std::string& operator_id, const std::string& request_id,
    const std::string& status, const std::string& message = "", const std::string& error_code = "",
    const std::string& failure_stage = "", std::uint64_t before_generation = 0,
    std::uint64_t after_generation = 0, std::uint64_t target_generation = 0);

PolicyAuditRecord make_manual_resolution_audit_record(const PolicyLifecycleEvent& event);

PolicyAuditRecord make_manual_resolution_audit_record(
    PolicyLifecycleEventType event_type, const PolicyResourceKey& key, const std::string& operation,
    const std::string& source_type, const std::string& operator_id, const std::string& request_id,
    const std::string& status, const std::string& message = "", const std::string& error_code = "",
    const std::string& failure_stage = "", std::uint64_t before_generation = 0,
    std::uint64_t after_generation = 0, std::uint64_t target_generation = 0);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_AUDIT_H

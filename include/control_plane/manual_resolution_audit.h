// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_AUDIT_H
#define BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_AUDIT_H

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"

#include <cstdint>
#include <string>

namespace bytetaper::control_plane {

std::uint64_t manual_resolution_audit_now_ms();

PolicyAuditRecord make_manual_resolution_audit_record(const PolicyResourceKey& key,
                                                      const std::string& operation,
                                                      const std::string& source_type,
                                                      const std::string& operator_id,
                                                      const std::string& request_id);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_MANUAL_RESOLUTION_AUDIT_H

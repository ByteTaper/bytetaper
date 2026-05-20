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

PolicyAuditRecord make_manual_resolution_audit_record(const PolicyResourceKey& key,
                                                      const std::string& operation,
                                                      const std::string& source_type,
                                                      const std::string& operator_id,
                                                      const std::string& request_id) {
    PolicyAuditRecord record{};
    record.record_version = 2;
    record.apply_id = request_id;
    record.resource_key = key.to_string();
    record.source_type = source_type;
    record.operator_id = operator_id;
    record.request_id = request_id;
    record.operation = operation;
    record.recorded_at_unix_epoch_ms = manual_resolution_audit_now_ms();
    return record;
}

} // namespace bytetaper::control_plane

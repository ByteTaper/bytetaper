// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_AUDIT_RETENTION_CONFIG_H
#define BYTETAPER_CONTROL_PLANE_AUDIT_RETENTION_CONFIG_H

#include <cstdint>

namespace bytetaper::control_plane {

struct AuditRetentionConfig {
    bool enabled = true;
    std::uint64_t retention_records = 10000;
    std::uint32_t retention_days = 30;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_AUDIT_RETENTION_CONFIG_H

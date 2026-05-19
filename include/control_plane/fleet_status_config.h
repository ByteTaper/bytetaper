// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_FLEET_STATUS_CONFIG_H
#define BYTETAPER_CONTROL_PLANE_FLEET_STATUS_CONFIG_H

#include <cstdint>

namespace bytetaper::control_plane {

struct FleetStatusConfig {
    std::uint32_t runtime_heartbeat_timeout_ms = 30000;
    std::uint32_t runtime_status_retention_ms = 300000;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_FLEET_STATUS_CONFIG_H

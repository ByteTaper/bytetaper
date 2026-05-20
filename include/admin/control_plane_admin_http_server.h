// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_ADMIN_CONTROL_PLANE_ADMIN_HTTP_SERVER_H
#define BYTETAPER_ADMIN_CONTROL_PLANE_ADMIN_HTTP_SERVER_H

#include "control_plane/control_plane_service.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::admin {

struct ControlPlaneAdminHttpServerConfig {
    const char* listen_address = "127.0.0.1";
    std::uint16_t port = 19090;
    control_plane::ControlPlaneService* control_plane_service = nullptr;
    std::size_t max_request_bytes = 1024 * 1024;
    bool strict_production_apply = true;
};

struct ControlPlaneAdminHttpServerHandle {
    void* impl = nullptr;
    std::uint16_t bound_port = 0;
};

bool start_control_plane_admin_http_server(const ControlPlaneAdminHttpServerConfig& config,
                                           ControlPlaneAdminHttpServerHandle* handle);

void stop_control_plane_admin_http_server(ControlPlaneAdminHttpServerHandle* handle);

} // namespace bytetaper::admin

#endif // BYTETAPER_ADMIN_CONTROL_PLANE_ADMIN_HTTP_SERVER_H

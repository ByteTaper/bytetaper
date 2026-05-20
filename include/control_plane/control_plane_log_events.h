// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_LOG_EVENTS_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_LOG_EVENTS_H

#include "control_plane/policy_lifecycle_event.h"

#include <string>

namespace bytetaper::control_plane {

std::string format_control_plane_lifecycle_log_json(const PolicyLifecycleEvent& event);

void log_control_plane_lifecycle_event(const PolicyLifecycleEvent& event);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_LOG_EVENTS_H

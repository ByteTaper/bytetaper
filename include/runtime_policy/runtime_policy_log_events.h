// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_LOG_EVENTS_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_LOG_EVENTS_H

#include "control_plane/policy_lifecycle_event.h"

#include <string>

namespace bytetaper::runtime_policy {

std::string
format_runtime_policy_lifecycle_log_json(const control_plane::PolicyLifecycleEvent& event);

void log_runtime_policy_lifecycle_event(const control_plane::PolicyLifecycleEvent& event);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_POLICY_LOG_EVENTS_H

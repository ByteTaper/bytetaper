// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H

#include "control_plane/audit_retention_config.h"
#include "control_plane/control_plane_auth_provider.h"
#include "control_plane/control_plane_metrics.h"
#include "control_plane/control_plane_security_config.h"
#include "control_plane/fleet_status_config.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_lifecycle_emitter.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"

#include <functional>

namespace bytetaper::runtime_policy {
struct RuntimePolicyMetrics;
}

namespace bytetaper::control_plane {

class PolicyUpdateQueue;

struct ControlPlaneServiceConfig {
    PolicyStateStore* policy_state_store = nullptr;
    PolicyUpdateQueue* policy_update_queue = nullptr;
    FleetStatusConfig fleet_status{};
    ControlPlaneMetrics* control_plane_metrics = nullptr;
    runtime_policy::RuntimePolicyMetrics* runtime_policy_metrics = nullptr;
    PolicyLifecycleEmitter* lifecycle_emitter = nullptr;
    AuditRetentionConfig audit_retention{};
    bool lifecycle_observability_enabled = true;
    ControlPlaneSecurityConfig security{};
    ControlPlaneAuthProvider* auth_provider = nullptr;
    ControlPlaneAuthContext mutation_auth_context{};
    bool default_internal_auth = true;

    std::function<bool(const PolicyResourceKey&)> is_policy_inactive;

    std::function<RepairLocalHookResult(const PolicyRepairLocalRequest& request,
                                        const ActivePolicyPointer& committed)>
        repair_local_hook;

    AdoptLocalFetchHook adopt_local_fetch_hook;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_CONFIG_H

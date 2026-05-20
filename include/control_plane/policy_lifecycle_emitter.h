// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_LIFECYCLE_EMITTER_H
#define BYTETAPER_CONTROL_PLANE_POLICY_LIFECYCLE_EMITTER_H

#include "control_plane/audit_retention_config.h"
#include "control_plane/control_plane_metrics.h"
#include "control_plane/policy_lifecycle_event.h"
#include "control_plane/policy_state_store.h"

namespace bytetaper::runtime_policy {
struct RuntimePolicyMetrics;
}

namespace bytetaper::control_plane {

struct PolicyLifecycleEmitterConfig {
    PolicyStateStore* policy_state_store = nullptr;
    ControlPlaneMetrics* control_plane_metrics = nullptr;
    runtime_policy::RuntimePolicyMetrics* runtime_policy_metrics = nullptr;
    AuditRetentionConfig audit_retention{};
    bool audit_enabled = true;
    bool log_enabled = true;
    bool metrics_enabled = true;
};

struct PolicyLifecycleEmitResult {
    bool ok = true;
    bool audit_written = false;
    std::string error;
};

class PolicyLifecycleEmitter {
public:
    explicit PolicyLifecycleEmitter(PolicyLifecycleEmitterConfig config);

    PolicyLifecycleEmitResult emit(const PolicyLifecycleEvent& event);

    PolicyLifecycleEmitResult emit_and_persist_audit(const PolicyLifecycleEvent& event);

    void emit_metrics_and_logs(const PolicyLifecycleEvent& event);

private:
    void record_metrics(const PolicyLifecycleEvent& event);

    PolicyLifecycleEmitterConfig config_;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_LIFECYCLE_EMITTER_H

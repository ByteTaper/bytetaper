// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_lifecycle_emitter.h"

#include "control_plane/control_plane_log_events.h"
#include "control_plane/policy_state_key.h"

#include <cstring>

namespace bytetaper::control_plane {

namespace {

bool is_runtime_component_event(PolicyLifecycleEventType type) {
    switch (type) {
    case PolicyLifecycleEventType::PolicyActivationStarted:
    case PolicyLifecycleEventType::PolicyActivationSucceeded:
    case PolicyLifecycleEventType::PolicyActivationFailed:
    case PolicyLifecycleEventType::RuntimeConverged:
    case PolicyLifecycleEventType::RuntimeStale:
    case PolicyLifecycleEventType::RuntimeActivationFailed:
    case PolicyLifecycleEventType::PolicyMismatchDetected:
        return true;
    default:
        return false;
    }
}

bool is_success(const PolicyLifecycleEvent& event) {
    return event.status == "success" || (event.status.empty() && event.error_code.empty());
}

} // namespace

PolicyLifecycleEmitter::PolicyLifecycleEmitter(PolicyLifecycleEmitterConfig config)
    : config_(std::move(config)) {}

void PolicyLifecycleEmitter::record_metrics(const PolicyLifecycleEvent& event) {
    if (!config_.metrics_enabled) {
        return;
    }

    const bool success = is_success(event);

    if (config_.control_plane_metrics != nullptr) {
        switch (event.event_type) {
        case PolicyLifecycleEventType::PolicyApplyCommitted:
            record_policy_apply_success(config_.control_plane_metrics, event.duration_ms);
            break;
        case PolicyLifecycleEventType::PolicyApplyFailed:
            record_policy_apply_failure(config_.control_plane_metrics, event.stage.c_str(),
                                        event.error_code.c_str());
            break;
        case PolicyLifecycleEventType::PolicyVersionStored:
            config_.control_plane_metrics->policy_version_stored_total.fetch_add(
                1, std::memory_order_relaxed);
            break;
        case PolicyLifecycleEventType::PolicyActivePromoted:
            if (success) {
                config_.control_plane_metrics->active_pointer_promote_total.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                config_.control_plane_metrics->active_pointer_promote_failed_total.fetch_add(
                    1, std::memory_order_relaxed);
            }
            break;
        case PolicyLifecycleEventType::ManualRepairCompleted:
        case PolicyLifecycleEventType::ManualAdoptCompleted:
        case PolicyLifecycleEventType::PolicyRollbackCompleted:
            record_manual_operation(config_.control_plane_metrics, event.operation.c_str(),
                                    success);
            break;
        case PolicyLifecycleEventType::ManualRepairRequested:
            if (!success) {
                record_manual_operation(config_.control_plane_metrics, "repair-local", false);
            }
            break;
        case PolicyLifecycleEventType::ManualAdoptRequested:
            if (!success) {
                record_manual_operation(config_.control_plane_metrics, "adopt-local", false);
            }
            break;
        case PolicyLifecycleEventType::PolicyRollbackRequested:
            if (!success) {
                record_manual_operation(config_.control_plane_metrics, "rollback", false);
            }
            break;
        default:
            break;
        }
        if (event.error_code.find("CONFLICT") != std::string::npos ||
            event.error_code.find("PROMOTE_CONFLICT") != std::string::npos ||
            event.error_code.find("VERSION_CONFLICT") != std::string::npos) {
            record_policy_cas_conflict(config_.control_plane_metrics);
        }
    }
}

PolicyLifecycleEmitResult PolicyLifecycleEmitter::emit(const PolicyLifecycleEvent& event) {
    PolicyLifecycleEmitResult result{};
    record_metrics(event);

    if (config_.log_enabled && !is_runtime_component_event(event.event_type)) {
        log_control_plane_lifecycle_event(event);
    }

    if (config_.audit_enabled && config_.policy_state_store != nullptr &&
        config_.audit_retention.enabled) {
        PolicyResourceKey key;
        if (!parse_resource_key(event.resource_key, &key)) {
            key = PolicyResourceKey::default_runtime();
        }
        const PolicyAuditRecord audit = lifecycle_event_to_audit_record(event);
        const AppendAuditResult audit_res =
            config_.policy_state_store->append_audit_record(key, audit);
        result.audit_written = audit_res.ok;
        if (!audit_res.ok) {
            result.ok = false;
            result.error = audit_res.error.empty() ? "audit write failed" : audit_res.error;
        }
    }

    return result;
}

PolicyLifecycleEmitResult
PolicyLifecycleEmitter::emit_and_persist_audit(const PolicyLifecycleEvent& event) {
    return emit(event);
}

void PolicyLifecycleEmitter::emit_metrics_and_logs(const PolicyLifecycleEvent& event) {
    record_metrics(event);
    if (config_.log_enabled && !is_runtime_component_event(event.event_type)) {
        log_control_plane_lifecycle_event(event);
    }
}

} // namespace bytetaper::control_plane

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_GUARDRAILS_H
#define BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_GUARDRAILS_H

#include "control_plane/control_plane_auth_provider.h"
#include "control_plane/control_plane_deployment_mode.h"
#include "control_plane/control_plane_metrics.h"
#include "control_plane/control_plane_security_config.h"
#include "control_plane/policy_apply_status.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::control_plane {

struct StartupValidationResult {
    bool ok = true;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

struct BindValidationResult {
    bool ok = true;
    bool warning_only = false;
    std::string message;
};

struct GuardrailResult {
    bool allowed = true;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string error_code;
    std::string message;
    std::string security_reason;
};

enum class DangerousOperationKind : std::uint8_t {
    RepairLocal,
    AdoptLocal,
    Rollback,
    BootstrapImport,
};

ControlPlaneAuthMode parse_control_plane_auth_mode(const std::string& value);

bool is_public_bind_address(const std::string& bind_address);

StartupValidationResult validate_startup(const ControlPlaneSecurityConfig& security,
                                         bool policy_state_store_configured);

BindValidationResult validate_bind_address(const ControlPlaneSecurityConfig& security,
                                           const std::string& bind_address,
                                           ControlPlaneMetrics* metrics);

GuardrailResult check_mutation_allowed(const ControlPlaneSecurityConfig& security,
                                       ControlPlaneAuthProvider* auth_provider,
                                       const ControlPlaneAuthContext& context,
                                       ControlPlaneMetrics* metrics);

GuardrailResult check_dangerous_operation_confirmation(DangerousOperationKind kind, bool confirm,
                                                       bool confirm_divergent_adoption = false);

void record_security_rejection(ControlPlaneMetrics* metrics, const char* operation,
                               const char* reason, ControlPlaneDeploymentMode mode);

void emit_security_event(const ControlPlaneSecurityConfig& security,
                         const ControlPlaneAuthContext& context, const char* event_name,
                         const char* reason, const std::string& message);

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_CONTROL_PLANE_GUARDRAILS_H

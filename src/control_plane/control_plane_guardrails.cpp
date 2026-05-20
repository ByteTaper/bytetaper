// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_guardrails.h"

#include "control_plane/control_plane_security_log_events.h"
#include "control_plane/manual_resolution_api.h"

#include <cstring>

namespace bytetaper::control_plane {

namespace {

constexpr const char* kErrUnauthorized = "CONTROL_PLANE_UNAUTHORIZED";
constexpr const char* kErrMutationDisabled = "CONTROL_PLANE_MUTATION_DISABLED";
constexpr const char* kErrRuntimeOnlyMutation = "RUNTIME_ONLY_MUTATION_REJECTED";

void append_error(StartupValidationResult* result, const std::string& message) {
    result->ok = false;
    result->errors.push_back(message);
}

void append_warning(StartupValidationResult* result, const std::string& message) {
    result->warnings.push_back(message);
}

GuardrailResult make_rejection(PolicyApplyStatus status, const char* error_code,
                               const std::string& message, const char* reason) {
    GuardrailResult result{};
    result.allowed = false;
    result.status = status;
    result.error_code = error_code;
    result.message = message;
    result.security_reason = reason != nullptr ? reason : "";
    return result;
}

} // namespace

ControlPlaneAuthMode parse_control_plane_auth_mode(const std::string& value) {
    if (value == "static-token" || value == "static_token") {
        return ControlPlaneAuthMode::StaticToken;
    }
    if (value == "mtls") {
        return ControlPlaneAuthMode::Mtls;
    }
    if (value == "oidc") {
        return ControlPlaneAuthMode::Oidc;
    }
    if (value == "external") {
        return ControlPlaneAuthMode::External;
    }
    return ControlPlaneAuthMode::None;
}

bool is_public_bind_address(const std::string& bind_address) {
    return bind_address == "0.0.0.0" || bind_address == "::" || bind_address == "[::]";
}

StartupValidationResult validate_startup(const ControlPlaneSecurityConfig& security,
                                         bool policy_state_store_configured) {
    StartupValidationResult result{};

    if (security.enabled && !policy_state_store_configured) {
        const bool remote_runtime_client =
            security.runtime_role == RuntimeProcessRole::RuntimeOnly &&
            !security.control_plane_endpoint.empty();
        if (!remote_runtime_client) {
            append_error(&result, "control plane enabled but policy state store is not configured");
        }
    }

    if (is_public_bind_address(security.bind_address) && !security.allow_public_bind) {
        const std::string msg =
            "public bind address requires allow_public_bind=true (bind=" + security.bind_address +
            ")";
        if (is_production_deployment_mode(security.deployment_mode)) {
            append_error(&result, msg);
        } else {
            append_warning(&result, msg);
        }
    }

    if (security.runtime_role == RuntimeProcessRole::RuntimeOnly &&
        security.control_plane_endpoint.empty() &&
        runtime_only_requires_control_plane_endpoint(security.deployment_mode)) {
        append_error(&result, "runtime-only role requires control_plane_endpoint in " +
                                  std::string(to_string(security.deployment_mode)) + " mode");
    }

    if (is_production_deployment_mode(security.deployment_mode) && security.enabled) {
        if (!security.enabled_explicit) {
            append_error(&result, "production mode requires control plane enabled to be explicit");
        }
        if (!security.runtime_role_explicit) {
            append_error(&result, "production mode requires runtime role to be explicit");
        }
        if (security.auth.mode == ControlPlaneAuthMode::None &&
            !security.auth.allow_unauthenticated_production_mutation) {
            append_error(&result, "production mode requires static-token auth or "
                                  "allow_unauthenticated_production_mutation=true");
        }
        if (security.auth.mode == ControlPlaneAuthMode::Mtls ||
            security.auth.mode == ControlPlaneAuthMode::Oidc ||
            security.auth.mode == ControlPlaneAuthMode::External) {
            append_error(&result, "production mode auth mode is not implemented yet");
        }
    }

    if (security.auth.allow_unauthenticated_production_mutation &&
        security.auth.mode == ControlPlaneAuthMode::None) {
        append_warning(&result,
                       "unsafe production config: unauthenticated mutations explicitly allowed");
    }

    return result;
}

BindValidationResult validate_bind_address(const ControlPlaneSecurityConfig& security,
                                           const std::string& bind_address,
                                           ControlPlaneMetrics* metrics) {
    BindValidationResult result{};
    if (!is_public_bind_address(bind_address) || security.allow_public_bind) {
        return result;
    }

    result.message =
        "bind address " + bind_address + " is public; set allow_public_bind=true to opt in";
    if (is_production_deployment_mode(security.deployment_mode)) {
        result.ok = false;
        if (metrics != nullptr) {
            metrics->public_bind_rejected_total.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    result.warning_only = true;
    return result;
}

void record_security_rejection(ControlPlaneMetrics* metrics, const char* operation,
                               const char* reason, ControlPlaneDeploymentMode mode) {
    if (metrics == nullptr) {
        return;
    }
    metrics->mutation_rejected_total.fetch_add(1, std::memory_order_relaxed);
    if (reason != nullptr && std::strcmp(reason, "runtime_only") == 0) {
        metrics->runtime_only_mutation_rejected_total.fetch_add(1, std::memory_order_relaxed);
    }
    (void) operation;
    (void) mode;
}

void emit_security_event(const ControlPlaneSecurityConfig& security,
                         const ControlPlaneAuthContext& context, const char* event_name,
                         const char* reason, const std::string& message) {
    ControlPlaneSecurityLogEvent event{};
    event.event = event_name != nullptr ? event_name : "";
    event.operation = context.operation;
    event.resource_key = context.resource_key;
    event.remote_address = context.remote_address;
    event.reason = reason != nullptr ? reason : "";
    event.deployment_mode = to_string(security.deployment_mode);
    event.message = message;
    log_control_plane_security_event(event);
}

GuardrailResult check_mutation_allowed(const ControlPlaneSecurityConfig& security,
                                       ControlPlaneAuthProvider* auth_provider,
                                       const ControlPlaneAuthContext& context,
                                       ControlPlaneMetrics* metrics) {
    if (context.internal_call) {
        return GuardrailResult{};
    }

    if (!security.enabled) {
        record_security_rejection(metrics, context.operation.c_str(), "disabled",
                                  security.deployment_mode);
        emit_security_event(security, context, "mutation_rejected_disabled", "disabled",
                            "control plane mutations are disabled");
        return make_rejection(PolicyApplyStatus::RejectedInvalidRequest, kErrMutationDisabled,
                              "control plane is not enabled", "disabled");
    }

    if (security.runtime_role == RuntimeProcessRole::RuntimeOnly) {
        record_security_rejection(metrics, context.operation.c_str(), "runtime_only",
                                  security.deployment_mode);
        emit_security_event(security, context, "runtime_only_mutation_rejected", "runtime_only",
                            "mutation endpoints are disabled in runtime-only role");
        return make_rejection(PolicyApplyStatus::RejectedInvalidRequest, kErrRuntimeOnlyMutation,
                              "mutation endpoints are disabled in runtime-only role",
                              "runtime_only");
    }

    if (metrics != nullptr) {
        metrics->auth_requests_total.fetch_add(1, std::memory_order_relaxed);
    }

    if (auth_provider == nullptr) {
        if (is_production_deployment_mode(security.deployment_mode) &&
            security.auth.mode == ControlPlaneAuthMode::None &&
            !security.auth.allow_unauthenticated_production_mutation) {
            if (metrics != nullptr) {
                metrics->auth_denied_total.fetch_add(1, std::memory_order_relaxed);
                metrics->mutation_rejected_total.fetch_add(1, std::memory_order_relaxed);
            }
            emit_security_event(security, context, "mutation_rejected_unauthorized",
                                "missing_auth_config", "production mutation requires auth");
            return make_rejection(PolicyApplyStatus::RejectedInvalidRequest, kErrUnauthorized,
                                  "production mutation requires authentication", "missing_token");
        }
        return GuardrailResult{};
    }

    const AuthDecision decision = auth_provider->authorize(context);
    switch (decision) {
    case AuthDecision::Allow:
        return GuardrailResult{};
    case AuthDecision::Unauthenticated:
        if (metrics != nullptr) {
            metrics->auth_denied_total.fetch_add(1, std::memory_order_relaxed);
            metrics->mutation_rejected_total.fetch_add(1, std::memory_order_relaxed);
        }
        emit_security_event(security, context, "mutation_rejected_unauthorized", "missing_token",
                            "mutation request is not authenticated");
        return make_rejection(PolicyApplyStatus::RejectedInvalidRequest, kErrUnauthorized,
                              "mutation request requires authentication", "missing_token");
    case AuthDecision::Deny:
        if (metrics != nullptr) {
            metrics->auth_denied_total.fetch_add(1, std::memory_order_relaxed);
            metrics->mutation_rejected_total.fetch_add(1, std::memory_order_relaxed);
        }
        emit_security_event(security, context, "mutation_rejected_unauthorized", "invalid_token",
                            "mutation request authentication failed");
        return make_rejection(PolicyApplyStatus::RejectedInvalidRequest, kErrUnauthorized,
                              "mutation request authentication failed", "invalid_token");
    case AuthDecision::Unsupported:
        if (metrics != nullptr) {
            metrics->auth_denied_total.fetch_add(1, std::memory_order_relaxed);
            metrics->mutation_rejected_total.fetch_add(1, std::memory_order_relaxed);
        }
        emit_security_event(security, context, "mutation_rejected_unauthorized",
                            "unsupported_auth_mode", "configured auth mode is not supported");
        return make_rejection(PolicyApplyStatus::RejectedInvalidRequest, kErrUnauthorized,
                              "configured auth mode is not supported", "unsupported_auth");
    }
    return GuardrailResult{};
}

GuardrailResult check_dangerous_operation_confirmation(DangerousOperationKind kind, bool confirm,
                                                       bool confirm_divergent_adoption) {
    switch (kind) {
    case DangerousOperationKind::RepairLocal:
    case DangerousOperationKind::Rollback:
        if (confirm) {
            return GuardrailResult{};
        }
        break;
    case DangerousOperationKind::AdoptLocal:
        if (confirm_divergent_adoption) {
            return GuardrailResult{};
        }
        break;
    case DangerousOperationKind::BootstrapImport:
        if (confirm) {
            return GuardrailResult{};
        }
        break;
    }
    return make_rejection(PolicyApplyStatus::RejectedConfirmationRequired,
                          kErrManualConfirmationRequired,
                          "This operation requires explicit confirmation.", "missing_confirmation");
}

} // namespace bytetaper::control_plane

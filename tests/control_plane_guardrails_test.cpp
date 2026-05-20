// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_guardrails.h"
#include "control_plane/control_plane_metrics.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/static_token_auth_provider.h"

#include <gtest/gtest.h>

using namespace bytetaper::control_plane;

TEST(ControlPlaneGuardrailsTest, DefaultBindAddressIsNotPublic) {
    EXPECT_FALSE(is_public_bind_address("127.0.0.1"));
    EXPECT_TRUE(is_public_bind_address("0.0.0.0"));
}

TEST(ControlPlaneGuardrailsTest, PublicBindFailsProductionWithoutOptIn) {
    ControlPlaneSecurityConfig security{};
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.bind_address = "0.0.0.0";
    security.allow_public_bind = false;

    ControlPlaneMetrics metrics{};
    const BindValidationResult result =
        validate_bind_address(security, security.bind_address, &metrics);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(metrics.public_bind_rejected_total.load(), 1u);
}

TEST(ControlPlaneGuardrailsTest, PublicBindWarnsInLocalDev) {
    ControlPlaneSecurityConfig security{};
    security.deployment_mode = ControlPlaneDeploymentMode::LocalDev;
    security.bind_address = "0.0.0.0";

    const BindValidationResult result =
        validate_bind_address(security, security.bind_address, nullptr);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.warning_only);
}

namespace {

constexpr const char* kRuntimeOnlyMutationRejected = "RUNTIME_ONLY_MUTATION_REJECTED";

void expect_runtime_only_rejects_operation(const char* operation) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.runtime_role = RuntimeProcessRole::RuntimeOnly;

    ControlPlaneAuthContext context{};
    context.operation = operation;
    context.resource_key = "policy/default/runtime";
    context.deployment_mode = security.deployment_mode;

    ControlPlaneMetrics metrics{};
    const GuardrailResult result = check_mutation_allowed(security, nullptr, context, &metrics);
    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(result.error_code, kRuntimeOnlyMutationRejected);
    EXPECT_EQ(metrics.runtime_only_mutation_rejected_total.load(), 1u);
}

} // namespace

TEST(ControlPlaneGuardrailsTest, RuntimeOnlyRejectsPolicyApply) {
    expect_runtime_only_rejects_operation("policy_apply");
}

TEST(ControlPlaneGuardrailsTest, RuntimeOnlyRejectsRepairAdoptRollback) {
    expect_runtime_only_rejects_operation("repair-local");
    expect_runtime_only_rejects_operation("adopt-local");
    expect_runtime_only_rejects_operation("rollback");
}

class RuntimeOnlyRejectsMutationTest : public ::testing::TestWithParam<const char*> {};

TEST_P(RuntimeOnlyRejectsMutationTest, RejectsMutationOperation) {
    expect_runtime_only_rejects_operation(GetParam());
}

INSTANTIATE_TEST_SUITE_P(ControlPlaneGuardrailsTest, RuntimeOnlyRejectsMutationTest,
                         ::testing::Values("policy_apply", "repair-local", "adopt-local",
                                           "rollback"));

TEST(ControlPlaneGuardrailsTest, DangerousOperationRequiresConfirmation) {
    const GuardrailResult missing =
        check_dangerous_operation_confirmation(DangerousOperationKind::Rollback, false);
    EXPECT_FALSE(missing.allowed);
    EXPECT_EQ(missing.status, PolicyApplyStatus::RejectedConfirmationRequired);
    EXPECT_EQ(missing.error_code, kErrManualConfirmationRequired);

    const GuardrailResult ok =
        check_dangerous_operation_confirmation(DangerousOperationKind::Rollback, true);
    EXPECT_TRUE(ok.allowed);
}

TEST(ControlPlaneGuardrailsTest, ProductionStartupRequiresAuthOrUnsafeOverride) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.enabled_explicit = true;
    security.runtime_role_explicit = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::None;
    security.auth.allow_unauthenticated_production_mutation = false;

    const StartupValidationResult result = validate_startup(security, true);
    EXPECT_FALSE(result.ok);
}

TEST(ControlPlaneGuardrailsTest, ProductionStartupRequiresExplicitEnabled) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.enabled_explicit = false;
    security.runtime_role_explicit = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    const StartupValidationResult result = validate_startup(security, true);
    EXPECT_FALSE(result.ok);
}

TEST(ControlPlaneGuardrailsTest, ProductionStartupRequiresExplicitRuntimeRole) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.enabled_explicit = true;
    security.runtime_role_explicit = false;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    const StartupValidationResult result = validate_startup(security, true);
    EXPECT_FALSE(result.ok);
}

TEST(ControlPlaneGuardrailsTest, ProductionRuntimeOnlyRequiresControlPlaneEndpoint) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.enabled_explicit = true;
    security.runtime_role_explicit = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.runtime_role = RuntimeProcessRole::RuntimeOnly;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    const StartupValidationResult result = validate_startup(security, true);
    EXPECT_FALSE(result.ok);
}

TEST(ControlPlaneGuardrailsTest, MultiRuntimeRuntimeOnlyRequiresControlPlaneEndpoint) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.deployment_mode = ControlPlaneDeploymentMode::MultiRuntime;
    security.runtime_role = RuntimeProcessRole::RuntimeOnly;

    const StartupValidationResult result = validate_startup(security, true);
    EXPECT_FALSE(result.ok);
}

TEST(ControlPlaneGuardrailsTest, MultiRuntimeRuntimeOnlyAllowsRemoteControlPlaneWithoutLocalStore) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.deployment_mode = ControlPlaneDeploymentMode::MultiRuntime;
    security.runtime_role = RuntimeProcessRole::RuntimeOnly;
    security.control_plane_endpoint = "http://control-plane:19090";

    const StartupValidationResult result = validate_startup(security, false);
    EXPECT_TRUE(result.ok);
}

TEST(ControlPlaneGuardrailsTest, SingleNodeRuntimeOnlyAllowsMissingControlPlaneEndpoint) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.deployment_mode = ControlPlaneDeploymentMode::SingleNode;
    security.runtime_role = RuntimeProcessRole::RuntimeOnly;

    const StartupValidationResult result = validate_startup(security, true);
    EXPECT_TRUE(result.ok);
}

TEST(ControlPlaneGuardrailsTest, ProductionStartupOkWithExplicitConfigAndStaticToken) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.enabled_explicit = true;
    security.runtime_role_explicit = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    const StartupValidationResult result = validate_startup(security, true);
    EXPECT_TRUE(result.ok);
}

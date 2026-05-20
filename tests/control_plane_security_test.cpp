// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_guardrails.h"
#include "control_plane/control_plane_metrics.h"
#include "control_plane/control_plane_security_log_events.h"
#include "control_plane/control_plane_service.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_apply_api.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "control_plane/static_token_auth_provider.h"

#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <random>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;

namespace {

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_cp_sec_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

std::unique_ptr<ControlPlaneService> make_runtime_only_service(RocksDBPolicyStateStore& store) {
    ControlPlaneServiceConfig config{};
    config.policy_state_store = &store;
    config.security.enabled = true;
    config.security.runtime_role = RuntimeProcessRole::RuntimeOnly;
    config.default_internal_auth = false;
    config.mutation_auth_context.internal_call = false;
    return std::make_unique<ControlPlaneService>(config);
}

constexpr const char* kRuntimeOnlyMutationRejected = "RUNTIME_ONLY_MUTATION_REJECTED";

} // namespace

TEST(ControlPlaneSecurityTest, StaticTokenMissingRejectsProductionMutation) {
    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    ControlPlaneAuthConfig auth_config = security.auth;
    auto provider = make_auth_provider(auth_config, security.deployment_mode);
    ASSERT_NE(provider, nullptr);

    ControlPlaneAuthContext context{};
    context.operation = "policy_apply";
    context.resource_key = "policy/default/runtime";
    context.deployment_mode = security.deployment_mode;
    context.mutation = true;

    ControlPlaneMetrics metrics{};
    const GuardrailResult result =
        check_mutation_allowed(security, provider.get(), context, &metrics);
    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(metrics.auth_denied_total.load(), 1u);
}

TEST(ControlPlaneSecurityTest, StaticTokenValidAllowsProductionMutation) {
    constexpr const char* kToken = "test-secret-token-value";
    ::setenv("BYTETAPER_CONTROL_PLANE_TOKEN", kToken, 1);

    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    auto provider = make_auth_provider(security.auth, security.deployment_mode);
    ASSERT_NE(provider, nullptr);

    ControlPlaneAuthContext context{};
    context.operation = "policy_apply";
    context.resource_key = "policy/default/runtime";
    context.deployment_mode = security.deployment_mode;
    context.mutation = true;
    context.headers["Authorization"] = std::string("Bearer ") + kToken;

    const GuardrailResult result =
        check_mutation_allowed(security, provider.get(), context, nullptr);
    EXPECT_TRUE(result.allowed);

    const ControlPlaneSecurityLogEvent event{};
    const std::string json = format_control_plane_security_log_json(event);
    EXPECT_EQ(json.find(kToken), std::string::npos);
    EXPECT_EQ(json.find("Authorization"), std::string::npos);

    ::unsetenv("BYTETAPER_CONTROL_PLANE_TOKEN");
}

TEST(ControlPlaneSecurityTest, StaticTokenInvalidRejectsProductionMutation) {
    constexpr const char* kToken = "configured-secret-token";
    constexpr const char* kWrongToken = "wrong-bearer-token-value";
    ::setenv("BYTETAPER_CONTROL_PLANE_TOKEN", kToken, 1);

    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    auto provider = make_auth_provider(security.auth, security.deployment_mode);
    ASSERT_NE(provider, nullptr);

    ControlPlaneAuthContext context{};
    context.operation = "policy_apply";
    context.resource_key = "policy/default/runtime";
    context.deployment_mode = security.deployment_mode;
    context.mutation = true;
    context.headers["Authorization"] = std::string("Bearer ") + kWrongToken;

    ControlPlaneMetrics metrics{};
    const GuardrailResult result =
        check_mutation_allowed(security, provider.get(), context, &metrics);
    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(metrics.auth_denied_total.load(), 1u);
    EXPECT_EQ(metrics.mutation_rejected_total.load(), 1u);

    const ControlPlaneSecurityLogEvent event{};
    const std::string json = format_control_plane_security_log_json(event);
    EXPECT_EQ(json.find(kToken), std::string::npos);
    EXPECT_EQ(json.find(kWrongToken), std::string::npos);

    ::unsetenv("BYTETAPER_CONTROL_PLANE_TOKEN");
}

TEST(ControlPlaneSecurityTest, ServiceApplyRejectedWhenRuntimeOnly) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    std::unique_ptr<ControlPlaneService> service = make_runtime_only_service(store);

    PolicyApplyRequest request{};
    request.source = "apiVersion: bytetaper.io/v1alpha1\nkind: RuntimePolicy\n";
    request.request_id = "sec-apply-runtime-only";

    const PolicyApplySubmitResult result = service->apply(request);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("runtime-only"), std::string::npos);

    destroy_db(db_path);
}

TEST(ControlPlaneSecurityTest, ServiceRepairLocalRejectedWhenRuntimeOnly) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    std::unique_ptr<ControlPlaneService> service = make_runtime_only_service(store);

    PolicyRepairLocalRequest request{};
    request.request_id = "sec-repair-runtime-only";

    const PolicyRepairLocalResult result = service->repair_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kRuntimeOnlyMutationRejected);

    destroy_db(db_path);
}

TEST(ControlPlaneSecurityTest, ServiceAdoptLocalRejectedWhenRuntimeOnly) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    std::unique_ptr<ControlPlaneService> service = make_runtime_only_service(store);

    PolicyAdoptLocalRequest request{};
    request.request_id = "sec-adopt-runtime-only";

    const PolicyAdoptLocalResult result = service->adopt_local(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kRuntimeOnlyMutationRejected);

    destroy_db(db_path);
}

TEST(ControlPlaneSecurityTest, ServiceRollbackRejectedWhenRuntimeOnly) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    std::unique_ptr<ControlPlaneService> service = make_runtime_only_service(store);

    PolicyRollbackRequest request{};
    request.request_id = "sec-rollback-runtime-only";

    const PolicyRollbackResult result = service->rollback(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, kRuntimeOnlyMutationRejected);

    destroy_db(db_path);
}

TEST(ControlPlaneSecurityTest, RuntimeOnlyRejectsRepairAdoptRollback) {
    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);
    RocksDBPolicyStateStore store(db_path.c_str());
    ASSERT_TRUE(store.is_open());

    std::unique_ptr<ControlPlaneService> service = make_runtime_only_service(store);

    PolicyRepairLocalRequest repair{};
    repair.request_id = "sec-repair-adopt-rollback-bundle";
    const PolicyRepairLocalResult repair_result = service->repair_local(repair);
    EXPECT_FALSE(repair_result.ok);
    EXPECT_EQ(repair_result.error_code, kRuntimeOnlyMutationRejected);

    PolicyAdoptLocalRequest adopt{};
    adopt.request_id = "sec-repair-adopt-rollback-bundle";
    const PolicyAdoptLocalResult adopt_result = service->adopt_local(adopt);
    EXPECT_FALSE(adopt_result.ok);
    EXPECT_EQ(adopt_result.error_code, kRuntimeOnlyMutationRejected);

    PolicyRollbackRequest rollback{};
    rollback.request_id = "sec-repair-adopt-rollback-bundle";
    const PolicyRollbackResult rollback_result = service->rollback(rollback);
    EXPECT_FALSE(rollback_result.ok);
    EXPECT_EQ(rollback_result.error_code, kRuntimeOnlyMutationRejected);

    destroy_db(db_path);
}

TEST(ControlPlaneSecurityTest, InternalCallBypassesProductionAuth) {
    constexpr const char* kToken = "internal-bypass-token";
    ::setenv("BYTETAPER_CONTROL_PLANE_TOKEN", kToken, 1);

    ControlPlaneSecurityConfig security{};
    security.enabled = true;
    security.deployment_mode = ControlPlaneDeploymentMode::Production;
    security.auth.mode = ControlPlaneAuthMode::StaticToken;

    auto provider = make_auth_provider(security.auth, security.deployment_mode);
    ASSERT_NE(provider, nullptr);

    ControlPlaneAuthContext missing_token{};
    missing_token.operation = "policy_apply";
    missing_token.deployment_mode = security.deployment_mode;
    missing_token.mutation = true;
    EXPECT_FALSE(check_mutation_allowed(security, provider.get(), missing_token, nullptr).allowed);

    ControlPlaneAuthContext authenticated = missing_token;
    authenticated.headers["Authorization"] = std::string("Bearer ") + kToken;
    EXPECT_TRUE(check_mutation_allowed(security, provider.get(), authenticated, nullptr).allowed);

    ControlPlaneAuthContext internal_ctx = authenticated;
    internal_ctx.internal_call = true;
    EXPECT_TRUE(check_mutation_allowed(security, provider.get(), internal_ctx, nullptr).allowed);

    ::unsetenv("BYTETAPER_CONTROL_PLANE_TOKEN");
}

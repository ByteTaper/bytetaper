// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/control_plane_auth_provider.h"
#include "control_plane/control_plane_service.h"
#include "control_plane/manual_resolution_api.h"
#include "control_plane/policy_apply_api.h"
#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_record.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "taperquery/policy_ir_identity.h"
#include "taperquery/policy_ir_yaml_emitter.h"
#include "taperquery/policy_persistence.h"

#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace bytetaper::control_plane;
using namespace bytetaper::taperquery;

namespace {

constexpr const char* kIntegrationToken = "integration-test-token";

std::string make_temp_db_path() {
    static std::mt19937_64 rng{ std::random_device{}() };
    return (fs::temp_directory_path() / ("bt_cp_sec_it_" + std::to_string(rng()))).string();
}

void destroy_db(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

std::string extproc_server_binary_path() {
    if (const char* env = std::getenv("BYTETAPER_EXTPROC_SERVER_BINARY");
        env != nullptr && env[0] != '\0') {
        if (fs::exists(env)) {
            return env;
        }
    }
    const fs::path candidates[] = {
        fs::path("build/control-plane-integration/bytetaper-extproc-server"),
        fs::path("/workspace/build/control-plane-integration/bytetaper-extproc-server"),
        fs::path("build/bytetaper-extproc-server"),
        fs::path("/workspace/build/bytetaper-extproc-server"),
    };
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate.string();
        }
    }
    return {};
}

TqPolicyDocument make_policy_doc(const std::string& route_id, std::uint64_t generation) {
    TqPolicyDocument doc;
    doc.document_id = "cp-sec-it";
    doc.source_name = "test";
    doc.generation = generation;
    doc.schema_version_num = 1;
    doc.api_version = "bytetaper.io/v1alpha1";
    doc.kind = "RuntimePolicy";

    TqRoutePolicy route;
    route.route_id = route_id;
    route.match_prefix = "/api";
    route.match_kind = TqRouteMatchKind::Prefix;
    route.mutation = TqMutationMode::Disabled;
    route.allowed_method = TqHttpMethod::Get;
    doc.routes.push_back(route);
    doc.policy_id = compute_policy_document_identity(doc);
    return doc;
}

std::string canonical_hash_for_yaml(const std::string& yaml) {
    return "sha256:" + compute_canonical_yaml_sha256_hex(yaml);
}

void seed_generation(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                     const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    PolicyVersionRecord version;
    version.generation = generation;
    version.policy_id = doc.policy_id;
    version.schema_version = 1;
    version.api_version = doc.api_version;
    version.kind = doc.kind;
    version.source_type = "taperql-apply";
    version.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    ASSERT_TRUE(store.store_policy_version(key, version, emit.yaml).ok);
}

void promote_generation(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                        const TqPolicyDocument& doc, std::uint64_t generation) {
    PolicyIrYamlEmitResult emit = emit_policy_ir_canonical_yaml(doc);
    ASSERT_TRUE(emit.ok);

    ActivePolicyPointer pointer;
    pointer.generation = generation;
    pointer.policy_id = doc.policy_id;
    pointer.canonical_hash = canonical_hash_for_yaml(emit.yaml);
    pointer.version_key = make_version_key(key, generation);
    pointer.yaml_key = make_yaml_key(key, generation);
    pointer.schema_version = 1;
    pointer.api_version = doc.api_version;
    pointer.kind = doc.kind;
    pointer.source_type = "taperql-apply";

    ExpectedActivePolicy expected;
    const auto active_res = store.load_active_pointer(key);
    if (active_res.ok) {
        expected.generation = active_res.pointer.generation;
        expected.policy_id = active_res.pointer.policy_id;
    } else {
        expected.generation = 0;
    }
    ASSERT_TRUE(store.compare_and_promote_active(key, expected, pointer).ok);
}

void seed_active_policy(RocksDBPolicyStateStore& store, const PolicyResourceKey& key,
                        const TqPolicyDocument& doc, std::uint64_t generation) {
    seed_generation(store, key, doc, generation);
    promote_generation(store, key, doc, generation);
}

void configure_production_security(ControlPlaneServiceConfig& config) {
    config.security.enabled = true;
    config.security.enabled_explicit = true;
    config.security.runtime_role_explicit = true;
    config.security.deployment_mode = ControlPlaneDeploymentMode::Production;
    config.security.auth.mode = ControlPlaneAuthMode::StaticToken;
    config.default_internal_auth = false;
    config.mutation_auth_context.internal_call = false;
    config.mutation_auth_context.deployment_mode = ControlPlaneDeploymentMode::Production;
}

struct SpawnResult {
    int exit_code = -1;
    std::string stderr_text;
};

SpawnResult
spawn_extproc_server(const std::vector<std::pair<std::string, std::string>>& env_overrides,
                     const std::vector<std::string>& args, int timeout_ms) {
    SpawnResult result{};
    const std::string binary = extproc_server_binary_path();
    if (binary.empty()) {
        return result;
    }

    int stderr_pipe[2];
    if (pipe(stderr_pipe) != 0) {
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        close(stderr_pipe[0]);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[1]);

        for (const auto& [key, value] : env_overrides) {
            ::setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(binary.c_str()));
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execv(binary.c_str(), argv.data());
        _exit(127);
    }

    close(stderr_pipe[1]);
    char buffer[512];
    ssize_t nbytes = 0;
    while ((nbytes = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
        result.stderr_text.append(buffer, static_cast<std::size_t>(nbytes));
    }
    close(stderr_pipe[0]);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else {
                result.exit_code = 1;
            }
            return result;
        }
        if (waited < 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
    result.exit_code = 1;
    return result;
}

bool tcp_connect_succeeds(const std::string& host, std::uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return false;
    }

    const bool connected = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    close(fd);
    return connected;
}

} // namespace

class ControlPlaneSecurityIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = make_temp_db_path();
        destroy_db(db_path_);
        store_ = std::make_unique<RocksDBPolicyStateStore>(db_path_.c_str());
        ASSERT_TRUE(store_->is_open());
        key_ = PolicyResourceKey::default_runtime();
    }

    void TearDown() override {
        service_.reset();
        store_.reset();
        destroy_db(db_path_);
        ::unsetenv("BYTETAPER_CONTROL_PLANE_TOKEN");
    }

    void build_production_service(const std::optional<std::string>& bearer_token = std::nullopt) {
        ::setenv("BYTETAPER_CONTROL_PLANE_TOKEN", kIntegrationToken, 1);
        ControlPlaneServiceConfig config{};
        config.policy_state_store = store_.get();
        configure_production_security(config);
        if (bearer_token.has_value()) {
            config.mutation_auth_context.headers["Authorization"] = *bearer_token;
        }
        service_ = std::make_unique<ControlPlaneService>(config);
    }

    void build_local_service() {
        ControlPlaneServiceConfig config{};
        config.policy_state_store = store_.get();
        config.security.enabled = true;
        config.security.deployment_mode = ControlPlaneDeploymentMode::LocalDev;
        config.default_internal_auth = false;
        config.mutation_auth_context.internal_call = false;
        service_ = std::make_unique<ControlPlaneService>(config);
    }

    std::string db_path_;
    PolicyResourceKey key_;
    std::unique_ptr<RocksDBPolicyStateStore> store_;
    std::unique_ptr<ControlPlaneService> service_;
};

TEST_F(ControlPlaneSecurityIntegrationTest, ProductionUnauthenticatedApplyRejected) {
    build_production_service();

    PolicyApplyRequest request{};
    request.source = "apiVersion: bytetaper.io/v1alpha1\nkind: RuntimePolicy\n";
    request.request_id = "it-unauth-apply";

    const PolicyApplySubmitResult result = service_->apply(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedInvalidRequest);
    EXPECT_NE(result.error.find("authentication"), std::string::npos);
}

TEST_F(ControlPlaneSecurityIntegrationTest, ProductionAuthenticatedApplyPassesGuardrail) {
    build_production_service(std::string("Bearer ") + kIntegrationToken);

    PolicyApplyRequest request{};
    request.source = "apiVersion: bytetaper.io/v1alpha1\nkind: RuntimePolicy\n";
    request.request_id = "it-auth-apply";

    const PolicyApplySubmitResult result = service_->apply(request);
    EXPECT_EQ(result.error.find("authentication"), std::string::npos);
}

TEST_F(ControlPlaneSecurityIntegrationTest, RollbackWithoutConfirmRejected) {
    build_local_service();
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    seed_active_policy(*store_, key_, gen1, 1);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    request.expected_current_generation = 1;
    request.expected_current_policy_id = gen1.policy_id;
    request.request_id = "it-rollback-no-confirm";
    request.confirm = false;

    const PolicyRollbackResult result = service_->rollback(request);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, PolicyApplyStatus::RejectedConfirmationRequired);
    EXPECT_EQ(result.error_code, kErrManualConfirmationRequired);
}

TEST_F(ControlPlaneSecurityIntegrationTest, RollbackWithConfirmAccepted) {
    build_local_service();
    const TqPolicyDocument gen1 = make_policy_doc("route-a", 1);
    const TqPolicyDocument gen2 = make_policy_doc("route-b", 2);
    const TqPolicyDocument gen3 = make_policy_doc("route-c", 3);
    seed_active_policy(*store_, key_, gen1, 1);
    seed_generation(*store_, key_, gen2, 2);
    promote_generation(*store_, key_, gen2, 2);
    seed_generation(*store_, key_, gen3, 3);
    promote_generation(*store_, key_, gen3, 3);

    PolicyRollbackRequest request{};
    request.resource_key = key_;
    request.target_generation = 1;
    request.expected_current_generation = 3;
    request.expected_current_policy_id = gen3.policy_id;
    request.request_id = "it-rollback-confirm";
    request.confirm = true;

    const PolicyRollbackResult result = service_->rollback(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.status, PolicyApplyStatus::RollbackAccepted);
    EXPECT_EQ(result.new_generation, 4u);
}

TEST(ControlPlaneSecurityProcessIntegrationTest, ProductionUnsafePublicBindStartupFails) {
    const std::string binary = extproc_server_binary_path();
    if (binary.empty()) {
        GTEST_SKIP() << "bytetaper-extproc-server binary not built";
    }

    const std::string db_path = make_temp_db_path();
    destroy_db(db_path);

    const std::vector<std::pair<std::string, std::string>> env = {
        { "BYTETAPER_DEPLOYMENT_MODE", "production" },
        { "BYTETAPER_CONTROL_PLANE_ENABLED", "1" },
        { "BYTETAPER_RUNTIME_ROLE", "control-plane" },
        { "BYTETAPER_CONTROL_PLANE_AUTH_MODE", "static-token" },
        { "BYTETAPER_CONTROL_PLANE_TOKEN", kIntegrationToken },
    };
    const std::vector<std::string> args = {
        "--listen-address",  "127.0.0.1:29180", "--metrics-address", "127.0.0.1",
        "--metrics-port",    "29181",           "--admin-address",   "0.0.0.0",
        "--policy-state-db", db_path,
    };

    const SpawnResult result = spawn_extproc_server(env, args, 5000);
    destroy_db(db_path);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.stderr_text.find("bind validation failed") != std::string::npos ||
                result.stderr_text.find("startup error") != std::string::npos ||
                result.stderr_text.find("public bind") != std::string::npos);
}

TEST(ControlPlaneSecurityProcessIntegrationTest, RuntimeOnlyDoesNotExposeAdminApply) {
    const std::string binary = extproc_server_binary_path();
    if (binary.empty()) {
        GTEST_SKIP() << "bytetaper-extproc-server binary not built";
    }

    const std::vector<std::pair<std::string, std::string>> env = {
        { "BYTETAPER_RUNTIME_ROLE", "runtime-only" },
    };
    const std::vector<std::string> args = {
        "--listen-address",
        "127.0.0.1:29280",
        "--metrics-address",
        "127.0.0.1",
        "--metrics-port",
        "29281",
        "--admin-enable-taperquery",
        "--admin-address",
        "127.0.0.1",
        "--admin-port",
        "29282",
    };

    const pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        for (const auto& [key, value] : env) {
            ::setenv(key.c_str(), value.c_str(), 1);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(binary.c_str()));
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(binary.c_str(), argv.data());
        _exit(127);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    EXPECT_FALSE(tcp_connect_succeeds("127.0.0.1", 29282));

    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
}

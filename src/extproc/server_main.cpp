// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "admin/taperquery_admin_http_server.h"
#include "bytetaper_build_info.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/inflight_registry.h"
#include "control_plane/control_plane_service.h"
#include "control_plane/rocksdb_policy_state_store.h"
#include "extproc/grpc_server.h"
#include "extproc/startup_parse.h"
#include "hash/hash.h"
#include "metrics/metrics_http_server.h"
#include "metrics/prometheus_registry.h"
#include "observability/logger.h"
#include "runtime/route_cache_epoch_store.h"
#include "runtime_policy/control_plane_policy_client.h"
#include "runtime_policy/policy_inactive_mode.h"
#include "runtime_policy/runtime_policy_plane.h"
#include "taperquery/policy_persistence.h"
#include "taperquery/tq_apply_audit.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

namespace {

std::atomic<bool> g_should_stop{ false };

void on_signal(int) {
    g_should_stop.store(true);
}

struct ServerArgs {
    const char* listen_address = "0.0.0.0:18080";
    const char* policy_file = nullptr;
    const char* l2_cache_path = nullptr;
    const char* metrics_listen_address = "0.0.0.0";
    std::uint16_t metrics_port = 18081;
    const char* admin_address = "127.0.0.1";
    std::uint16_t admin_port = 18082;
    bool admin_enable_taperquery = false;
    const char* admin_taperquery_state_dir = nullptr;
    const char* admin_taperquery_active_policy_file = "active-policy.yaml";
    const char* admin_taperquery_metadata_file = "active-policy.meta.json";
    const char* policy_state_dir = nullptr;
    const char* policy_state_db = nullptr;
    const char* policy_inactive_mode = nullptr;
    bool disable_policy_persistence = false;
    bool help = false;
    bool error = false;
};

bytetaper::runtime_policy::PolicyInactiveMode parse_policy_inactive_mode(const char* value) {
    if (value == nullptr) {
        return bytetaper::runtime_policy::PolicyInactiveMode::PassThrough;
    }
    if (std::strcmp(value, "reject") == 0) {
        return bytetaper::runtime_policy::PolicyInactiveMode::Reject;
    }
    if (std::strcmp(value, "startup-fail") == 0) {
        return bytetaper::runtime_policy::PolicyInactiveMode::StartupFail;
    }
    return bytetaper::runtime_policy::PolicyInactiveMode::PassThrough;
}

ServerArgs parse_args(int argc, char** argv) {
    ServerArgs args{};

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr) {
            continue;
        }

        if (std::strcmp(arg, "--listen-address") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --listen-address\n");
                args.error = true;
                return args;
            }
            args.listen_address = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--policy-file") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --policy-file\n");
                args.error = true;
                return args;
            }
            args.policy_file = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--l2-cache-path") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --l2-cache-path\n");
                args.error = true;
                return args;
            }
            args.l2_cache_path = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--metrics-address") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --metrics-address\n");
                args.error = true;
                return args;
            }
            args.metrics_listen_address = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--metrics-port") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --metrics-port\n");
                args.error = true;
                return args;
            }
            if (!bytetaper::extproc::startup::parse_u16_port("--metrics-port", argv[i + 1],
                                                             &args.metrics_port)) {
                args.error = true;
                return args;
            }
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--admin-address") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --admin-address\n");
                args.error = true;
                return args;
            }
            args.admin_address = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--admin-port") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --admin-port\n");
                args.error = true;
                return args;
            }
            if (!bytetaper::extproc::startup::parse_u16_port("--admin-port", argv[i + 1],
                                                             &args.admin_port)) {
                args.error = true;
                return args;
            }
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--admin-enable-taperquery") == 0) {
            args.admin_enable_taperquery = true;
            continue;
        }

        if (std::strcmp(arg, "--admin-taperquery-state-dir") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --admin-taperquery-state-dir\n");
                args.error = true;
                return args;
            }
            args.admin_taperquery_state_dir = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--admin-taperquery-active-policy-file") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --admin-taperquery-active-policy-file\n");
                args.error = true;
                return args;
            }
            args.admin_taperquery_active_policy_file = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--admin-taperquery-metadata-file") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --admin-taperquery-metadata-file\n");
                args.error = true;
                return args;
            }
            args.admin_taperquery_metadata_file = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--policy-state-dir") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --policy-state-dir\n");
                args.error = true;
                return args;
            }
            args.policy_state_dir = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--disable-policy-persistence") == 0) {
            args.disable_policy_persistence = true;
            continue;
        }

        if (std::strcmp(arg, "--policy-state-db") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --policy-state-db\n");
                args.error = true;
                return args;
            }
            args.policy_state_db = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--policy-inactive-mode") == 0) {
            if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "missing value for --policy-inactive-mode\n");
                args.error = true;
                return args;
            }
            args.policy_inactive_mode = argv[i + 1];
            i += 1;
            continue;
        }

        if (std::strcmp(arg, "--help") == 0) {
            args.help = true;
            return args;
        }

        std::fprintf(stderr, "unknown argument: %s\n", arg);
        args.error = true;
        return args;
    }

    return args;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("ByteTaper extproc server\n"
                        "version:    %s\n"
                        "git_sha:    %s\n"
                        "build_date: %s\n"
                        "build_type: %s\n"
                        "license:    AGPL-3.0-only OR LicenseRef-Commercial\n",
                        BYTETAPER_VERSION, BYTETAPER_GIT_SHA, BYTETAPER_BUILD_DATE,
                        BYTETAPER_BUILD_TYPE);
            return 0;
        }
    }

    bytetaper::observability::LoggerConfig log_config{};
    log_config.enabled = true;
    log_config.level = bytetaper::observability::LogLevel::Info;
    bytetaper::observability::logger_init(log_config);

    const char* seed_env = std::getenv("BYTETAPER_HASH_SEED_HEX");
    if (seed_env != nullptr && !bytetaper::hash::validate_hash_seed_hex(seed_env)) {
        std::fprintf(stderr, "error: invalid value for env var BYTETAPER_HASH_SEED_HEX\n");
        bytetaper::observability::logger_shutdown();
        return 1;
    }

    bytetaper::hash::init_process_hash_seed();

    ServerArgs args = parse_args(argc, argv);
    if (args.error) {
        bytetaper::observability::logger_shutdown();
        return 2;
    }
    if (args.help) {
        std::puts(
            "usage: bytetaper-extproc-server [--listen-address HOST:PORT] [--policy-file "
            "PATH] [--l2-cache-path PATH] [--metrics-address ADDR] [--metrics-port PORT] "
            "[--admin-address ADDR] [--admin-port PORT] [--admin-enable-taperquery] "
            "[--admin-taperquery-state-dir PATH] [--admin-taperquery-active-policy-file FILENAME] "
            "[--admin-taperquery-metadata-file FILENAME] [--policy-state-db PATH] "
            "[--policy-inactive-mode pass-through|reject|startup-fail]");
        bytetaper::observability::logger_shutdown();
        return 0;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    bytetaper::metrics::RuntimeHealthState health_state;
    bytetaper::metrics::MetricsRegistry metrics_registry{};
    bytetaper::metrics::MetricsHttpServerConfig metrics_config{};
    metrics_config.listen_address = args.metrics_listen_address;
    metrics_config.port = args.metrics_port;
    metrics_config.registry = &metrics_registry;
    metrics_config.health_state = &health_state;

    bytetaper::metrics::MetricsHttpServerHandle metrics_handle{};
    if (!bytetaper::metrics::start_metrics_http_server(metrics_config, &metrics_handle)) {
        std::fprintf(stderr, "failed to start metrics http server on %s:%u\n",
                     args.metrics_listen_address, args.metrics_port);
        // non-fatal, but good to know
    }

    auto l1_cache = std::make_unique<bytetaper::cache::L1Cache>();
    bytetaper::cache::l1_init(l1_cache.get());

    bytetaper::cache::L2DiskCache* l2_cache = nullptr;
    if (args.l2_cache_path != nullptr) {
        bytetaper::cache::L2CacheOptions l2_opts{};
        const char* l2_parse_err = nullptr;
        // See docs/runtime/CONFIGURATION.md for all supported env vars and valid ranges.
        if (!bytetaper::extproc::startup::parse_env_size("BYTETAPER_L2_BLOCK_CACHE_MB",
                                                         &l2_opts.block_cache_mb, &l2_parse_err) ||
            !bytetaper::extproc::startup::parse_env_size("BYTETAPER_L2_WRITE_BUFFER_MB",
                                                         &l2_opts.write_buffer_mb, &l2_parse_err) ||
            !bytetaper::extproc::startup::parse_env_positive_int(
                "BYTETAPER_L2_MAX_BACKGROUND_JOBS", &l2_opts.max_background_jobs, &l2_parse_err)) {
            std::fprintf(stderr, "error: invalid value for env var %s\n", l2_parse_err);
            health_state.not_ready_reason.store("L2 cache configuration parsing error",
                                                std::memory_order_release);
            bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
            bytetaper::observability::logger_shutdown();
            return 1;
        }

        std::fprintf(stderr,
                     "l2_cache: block_cache_mb=%zu write_buffer_mb=%zu max_background_jobs=%d\n",
                     l2_opts.block_cache_mb, l2_opts.write_buffer_mb, l2_opts.max_background_jobs);

        l2_cache = bytetaper::cache::l2_open_with_options(args.l2_cache_path, l2_opts);
        if (l2_cache == nullptr) {
            std::fprintf(stderr, "failed to open L2 cache at %s\n", args.l2_cache_path);
            health_state.not_ready_reason.store("failed to open L2 cache",
                                                std::memory_order_release);
            bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
            bytetaper::observability::logger_shutdown();
            return 4;
        }

        metrics_registry.runtime_metrics.l2_block_cache_mb_effective.store(
            l2_opts.block_cache_mb, std::memory_order_relaxed);
        metrics_registry.runtime_metrics.l2_write_buffer_mb_effective.store(
            l2_opts.write_buffer_mb, std::memory_order_relaxed);
        metrics_registry.runtime_metrics.l2_max_background_jobs_effective.store(
            static_cast<std::uint64_t>(l2_opts.max_background_jobs), std::memory_order_relaxed);
    }

    auto coalescing_registry = std::make_unique<bytetaper::coalescing::InFlightRegistry>();
    bytetaper::coalescing::registry_init(coalescing_registry.get());

    bytetaper::runtime::WorkerQueueConfig wq_config{};
    const char* parse_err = nullptr;
    if (!bytetaper::extproc::startup::parse_env_size("BYTETAPER_WORKER_COUNT",
                                                     &wq_config.worker_count, &parse_err) ||
        !bytetaper::extproc::startup::parse_env_size("BYTETAPER_LOOKUP_LANE_QUOTA",
                                                     &wq_config.lookup_lane_quota, &parse_err) ||
        !bytetaper::extproc::startup::parse_env_size("BYTETAPER_STORE_LANE_QUOTA",
                                                     &wq_config.store_lane_quota, &parse_err) ||
        !bytetaper::extproc::startup::parse_env_size("BYTETAPER_ASYNC_STORE_MAX_BODY_SIZE",
                                                     &wq_config.async_store_max_body_size,
                                                     &parse_err)) {
        std::fprintf(stderr, "error: invalid value for env var %s\n", parse_err);
        health_state.not_ready_reason.store("worker configuration parsing error",
                                            std::memory_order_release);
        bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
        if (l2_cache != nullptr) {
            bytetaper::cache::l2_close(&l2_cache);
        }
        bytetaper::observability::logger_shutdown();
        return 1;
    }

    auto policy_store = std::make_unique<bytetaper::runtime::RuntimePolicyStore>();

    bytetaper::taperquery::LocalPolicyPersistenceConfig persistence_config{};
    persistence_config.enabled = false;
    persistence_config.state_dir = "";

    // 1. Read environment variables
    const char* env_state_dir = std::getenv("BYTETAPER_POLICY_STATE_DIR");
    if (env_state_dir != nullptr) {
        persistence_config.state_dir = env_state_dir;
        persistence_config.enabled = true; // Enabled by default if state_dir is set in env
    }
    const char* env_enabled = std::getenv("BYTETAPER_POLICY_PERSISTENCE_ENABLED");
    if (env_enabled != nullptr) {
        std::string val(env_enabled);
        if (val == "1" || val == "true" || val == "yes" || val == "ON") {
            persistence_config.enabled = true;
        } else if (val == "0" || val == "false" || val == "no" || val == "OFF") {
            persistence_config.enabled = false;
        }
    }

    // 2. Override with CLI arguments
    if (args.policy_state_dir != nullptr) {
        persistence_config.enabled = true;
        persistence_config.state_dir = args.policy_state_dir;
    }
    if (args.admin_taperquery_state_dir != nullptr) {
        persistence_config.enabled = true;
        persistence_config.state_dir = args.admin_taperquery_state_dir;
    }

    // 3. Explicitly disable if --disable-policy-persistence is set
    if (args.disable_policy_persistence) {
        persistence_config.enabled = false;
        persistence_config.state_dir = "";
    }

    if (persistence_config.enabled) {
        persistence_config.active_policy_filename = args.admin_taperquery_active_policy_file;
        persistence_config.metadata_filename = args.admin_taperquery_metadata_file;

        if (persistence_config.state_dir.empty()) {
            std::fprintf(
                stderr,
                "error: taperquery active policy persistence is enabled, but no state directory is "
                "configured (use BYTETAPER_POLICY_STATE_DIR or --policy-state-dir)\n");
            bytetaper::observability::logger_shutdown();
            return 1;
        }
    }

    const char* env_policy_state_db = std::getenv("BYTETAPER_POLICY_STATE_DB");
    const char* policy_state_db_path = args.policy_state_db;
    if (policy_state_db_path == nullptr && env_policy_state_db != nullptr &&
        env_policy_state_db[0] != '\0') {
        policy_state_db_path = env_policy_state_db;
    }

    std::unique_ptr<bytetaper::control_plane::RocksDBPolicyStateStore> policy_state_store;
    if (policy_state_db_path != nullptr && policy_state_db_path[0] != '\0') {
        policy_state_store = std::make_unique<bytetaper::control_plane::RocksDBPolicyStateStore>(
            policy_state_db_path);
        if (!policy_state_store->is_open()) {
            std::fprintf(stderr, "error: failed to open policy state db at %s: %s\n",
                         policy_state_db_path, policy_state_store->open_error().c_str());
            bytetaper::observability::logger_shutdown();
            return 1;
        }
    }

    const char* env_policy_inactive_mode = std::getenv("BYTETAPER_POLICY_INACTIVE_MODE");
    const char* inactive_mode_value =
        args.policy_inactive_mode != nullptr ? args.policy_inactive_mode : env_policy_inactive_mode;

    bytetaper::runtime_policy::RuntimePolicyPlaneConfig policy_plane_config{};
    policy_plane_config.bootstrap_policy_file = args.policy_file;
    policy_plane_config.persistence_config = persistence_config;
    policy_plane_config.runtime_policy_store = policy_store.get();
    policy_plane_config.policy_state_store = policy_state_store.get();
    policy_plane_config.inactive_config.when_inactive =
        parse_policy_inactive_mode(inactive_mode_value);

    bytetaper::runtime_policy::RuntimePolicyPlane policy_plane(policy_plane_config);
    auto policy_start = policy_plane.start();

    if (!policy_start.ok) {
        health_state.not_ready_reason.store("policy startup failed", std::memory_order_release);
        bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
        if (l2_cache != nullptr) {
            bytetaper::cache::l2_close(&l2_cache);
        }
        bytetaper::observability::logger_shutdown();
        return 1;
    }

    std::unique_ptr<bytetaper::taperquery::TqApplyService> apply_service;
    std::unique_ptr<bytetaper::taperquery::TqApplyAuditStore> audit_store;
    bytetaper::admin::TaperQueryAdminHttpServerHandle admin_handle{};
    bool admin_started = false;

    auto route_cache_epoch_store = std::make_unique<bytetaper::runtime::RouteCacheEpochStore>();
    auto route_cache_cleanup_queue =
        std::make_unique<bytetaper::taperquery::RouteCacheCleanupQueueImpl>(
            l2_cache, &metrics_registry.cache_metrics);
    route_cache_cleanup_queue->start_worker();

    std::unique_ptr<bytetaper::control_plane::ControlPlaneService> control_plane_service;
    std::unique_ptr<bytetaper::runtime_policy::InProcessControlPlanePolicyClient> cp_policy_client;
    bool policy_pull_enabled = policy_state_store != nullptr;
    if (const char* env_pull = std::getenv("BYTETAPER_POLICY_PULL_ENABLED"); env_pull != nullptr) {
        const std::string pull_val(env_pull);
        policy_pull_enabled =
            pull_val == "1" || pull_val == "true" || pull_val == "yes" || pull_val == "ON";
    }
    if (policy_state_store != nullptr && policy_pull_enabled) {
        bytetaper::control_plane::ControlPlaneServiceConfig cp_config{};
        cp_config.policy_state_store = policy_state_store.get();
        control_plane_service =
            std::make_unique<bytetaper::control_plane::ControlPlaneService>(cp_config);
        cp_policy_client =
            std::make_unique<bytetaper::runtime_policy::InProcessControlPlanePolicyClient>(
                control_plane_service.get());

        bytetaper::runtime_policy::RuntimePolicyPullLoopConfig pull_loop_config{};
        pull_loop_config.pull.enabled = true;
        pull_loop_config.pull.resource_key =
            bytetaper::control_plane::PolicyResourceKey::default_runtime();
        pull_loop_config.pull.local_mirror = persistence_config;
        pull_loop_config.client = cp_policy_client.get();
        pull_loop_config.runtime_policy_store = policy_store.get();
        pull_loop_config.activation_barrier.policy_state_store = policy_state_store.get();
        pull_loop_config.activation_barrier.runtime_policy_store = policy_store.get();
        pull_loop_config.activation_barrier.route_cache_epoch_store = route_cache_epoch_store.get();
        pull_loop_config.activation_barrier.l1_cache = l1_cache.get();
        pull_loop_config.activation_barrier.l2_cleanup_queue = route_cache_cleanup_queue.get();
        pull_loop_config.activation_barrier.resource_key =
            bytetaper::control_plane::PolicyResourceKey::default_runtime();

        if (const char* env_interval = std::getenv("BYTETAPER_POLICY_PULL_INTERVAL_MS");
            env_interval != nullptr && env_interval[0] != '\0') {
            pull_loop_config.pull.pull_interval_ms =
                static_cast<std::uint32_t>(std::strtoul(env_interval, nullptr, 10));
        }

        policy_plane.start_pull_loop(pull_loop_config);
    }

    if (args.admin_enable_taperquery) {
        audit_store = std::make_unique<bytetaper::taperquery::TqApplyAuditStore>();
        apply_service = std::make_unique<bytetaper::taperquery::TqApplyService>(
            policy_store.get(), nullptr, audit_store.get(), persistence_config,
            route_cache_epoch_store.get(), route_cache_cleanup_queue.get(), l1_cache.get());
        bytetaper::admin::TaperQueryAdminHttpServerConfig admin_config{};
        admin_config.listen_address = args.admin_address;
        admin_config.port = args.admin_port;
        admin_config.policy_store = policy_store.get();
        admin_config.apply_service = apply_service.get();
        admin_config.audit_store = audit_store.get();
        admin_config.enable_taperquery_apply = true;

        if (bytetaper::admin::start_taperquery_admin_http_server(admin_config, &admin_handle)) {
            admin_started = true;
        } else {
            std::fprintf(stderr, "failed to start taperquery admin http server on %s:%u\n",
                         args.admin_address, args.admin_port);
        }
    }

    bytetaper::extproc::GrpcServerConfig config{};
    config.listen_address = args.listen_address;
    config.l1_cache = l1_cache.get();
    config.l2_cache = l2_cache;
    config.metrics_registry = &metrics_registry;
    config.coalescing_registry = coalescing_registry.get();
    config.wq_config = wq_config;
    config.policy_store = policy_store.get();
    config.route_cache_epoch_store = route_cache_epoch_store.get();

    bytetaper::extproc::GrpcServerHandle handle{};
    if (!bytetaper::extproc::start_grpc_server(config, &handle)) {
        if (handle.startup_error != nullptr) {
            std::fprintf(stderr, "failed to start extproc server on %s: %s\n", args.listen_address,
                         handle.startup_error);
            health_state.not_ready_reason.store(handle.startup_error, std::memory_order_release);
        } else {
            std::fprintf(stderr, "failed to start extproc server on %s\n", args.listen_address);
            health_state.not_ready_reason.store("gRPC startup error", std::memory_order_release);
        }
        if (admin_started) {
            bytetaper::admin::stop_taperquery_admin_http_server(&admin_handle);
        }
        bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
        if (l2_cache != nullptr) {
            bytetaper::cache::l2_close(&l2_cache);
        }
        bytetaper::observability::logger_shutdown();
        return 1;
    }

    health_state.ready.store(true, std::memory_order_release);

    std::fprintf(
        stderr, "worker_queue: workers=%zu lookup_quota=%zu store_quota=%zu store_max_body=%zu\n",
        handle.effective_wq_config.worker_count, handle.effective_wq_config.lookup_lane_quota,
        handle.effective_wq_config.store_lane_quota,
        handle.effective_wq_config.async_store_max_body_size);

    char buf[512];
    std::snprintf(buf, sizeof(buf), "bytetaper-extproc-server listening on %s (L1 enabled, L2 %s)",
                  args.listen_address, l2_cache ? "enabled" : "disabled");
    bytetaper::observability::log_info(buf);

    std::snprintf(buf, sizeof(buf), "metrics server listening on %s:%u",
                  args.metrics_listen_address, metrics_handle.bound_port);
    bytetaper::observability::log_info(buf);

    if (admin_started) {
        std::snprintf(buf, sizeof(buf), "admin server listening on %s:%u", args.admin_address,
                      admin_handle.bound_port);
        bytetaper::observability::log_info(buf);
    }

    while (!g_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    bytetaper::observability::log_info("exiting main loop, stopping server");

    health_state.not_ready_reason.store("shutting down", std::memory_order_release);
    health_state.ready.store(false, std::memory_order_release);

    policy_plane.stop_pull_loop();
    bytetaper::extproc::stop_grpc_server(&handle);
    if (admin_started) {
        bytetaper::admin::stop_taperquery_admin_http_server(&admin_handle);
    }
    bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
    if (l2_cache != nullptr) {
        bytetaper::cache::l2_close(&l2_cache);
    }
    bytetaper::observability::logger_shutdown();
    return 0;
}

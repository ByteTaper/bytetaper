// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/inflight_registry.h"
#include "extproc/grpc_server.h"
#include "extproc/startup_parse.h"
#include "hash/hash.h"
#include "metrics/metrics_http_server.h"
#include "metrics/prometheus_registry.h"
#include "observability/logger.h"
#include "policy/yaml_loader.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    bool help = false;
    bool error = false;
};

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
        std::puts("usage: bytetaper-extproc-server [--listen-address HOST:PORT] [--policy-file "
                  "PATH] [--l2-cache-path PATH] [--metrics-address ADDR] [--metrics-port PORT]");
        bytetaper::observability::logger_shutdown();
        return 0;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    bytetaper::policy::PolicyFileResult policy_result{};
    if (args.policy_file != nullptr) {
        if (!bytetaper::policy::load_policy_from_file(args.policy_file, &policy_result)) {
            std::fprintf(stderr, "failed to load policy file %s: %s\n", args.policy_file,
                         policy_result.error ? policy_result.error : "unknown error");
            bytetaper::observability::logger_shutdown();
            return 3;
        }
        if (policy_result.warning[0] != '\0') {
            std::fprintf(stderr, "policy warning: %s\n", policy_result.warning);
        }
        char buf[512];
        std::snprintf(buf, sizeof(buf), "loaded %zu routes from %s", policy_result.count,
                      args.policy_file);
        bytetaper::observability::log_info(buf);
    }

    auto l1_cache = std::make_unique<bytetaper::cache::L1Cache>();
    bytetaper::cache::l1_init(l1_cache.get());

    bytetaper::cache::L2DiskCache* l2_cache = nullptr;
    if (args.l2_cache_path != nullptr) {
        bytetaper::cache::L2CacheOptions l2_opts{};
        const char* l2_parse_err = nullptr;
        if (!bytetaper::extproc::startup::parse_env_size("BYTETAPER_L2_BLOCK_CACHE_MB",
                                                         &l2_opts.block_cache_mb, &l2_parse_err) ||
            !bytetaper::extproc::startup::parse_env_size("BYTETAPER_L2_WRITE_BUFFER_MB",
                                                         &l2_opts.write_buffer_mb, &l2_parse_err) ||
            !bytetaper::extproc::startup::parse_env_positive_int(
                "BYTETAPER_L2_MAX_BACKGROUND_JOBS", &l2_opts.max_background_jobs, &l2_parse_err)) {
            std::fprintf(stderr, "error: invalid value for env var %s\n", l2_parse_err);
            bytetaper::observability::logger_shutdown();
            return 1;
        }

        std::fprintf(stderr,
                     "l2_cache: block_cache_mb=%zu write_buffer_mb=%zu max_background_jobs=%d\n",
                     l2_opts.block_cache_mb, l2_opts.write_buffer_mb, l2_opts.max_background_jobs);

        l2_cache = bytetaper::cache::l2_open_with_options(args.l2_cache_path, l2_opts);
        if (l2_cache == nullptr) {
            std::fprintf(stderr, "failed to open L2 cache at %s\n", args.l2_cache_path);
            bytetaper::observability::logger_shutdown();
            return 4;
        }
    }

    auto coalescing_registry = std::make_unique<bytetaper::coalescing::InFlightRegistry>();
    bytetaper::coalescing::registry_init(coalescing_registry.get());

    bytetaper::metrics::MetricsRegistry metrics_registry{};
    bytetaper::metrics::MetricsHttpServerConfig metrics_config{};
    metrics_config.listen_address = args.metrics_listen_address;
    metrics_config.port = args.metrics_port;
    metrics_config.registry = &metrics_registry;

    bytetaper::metrics::MetricsHttpServerHandle metrics_handle{};
    if (!bytetaper::metrics::start_metrics_http_server(metrics_config, &metrics_handle)) {
        std::fprintf(stderr, "failed to start metrics http server on %s:%u\n",
                     args.metrics_listen_address, args.metrics_port);
        // non-fatal, but good to know
    }

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
        if (l2_cache != nullptr) {
            bytetaper::cache::l2_close(&l2_cache);
        }
        bytetaper::observability::logger_shutdown();
        return 1;
    }

    bytetaper::extproc::GrpcServerConfig config{};
    config.listen_address = args.listen_address;
    config.l1_cache = l1_cache.get();
    config.l2_cache = l2_cache;
    config.metrics_registry = &metrics_registry;
    config.coalescing_registry = coalescing_registry.get();
    config.wq_config = wq_config;
    if (policy_result.ok) {
        config.policies = policy_result.policies;
        config.policy_count = policy_result.count;
    }

    bytetaper::extproc::GrpcServerHandle handle{};
    if (!bytetaper::extproc::start_grpc_server(config, &handle)) {
        if (handle.startup_error != nullptr) {
            std::fprintf(stderr, "failed to start extproc server on %s: %s\n", args.listen_address,
                         handle.startup_error);
        } else {
            std::fprintf(stderr, "failed to start extproc server on %s\n", args.listen_address);
        }
        if (l2_cache != nullptr) {
            bytetaper::cache::l2_close(&l2_cache);
        }
        bytetaper::observability::logger_shutdown();
        return 1;
    }

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

    while (!g_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    bytetaper::observability::log_info("exiting main loop, stopping server");

    bytetaper::extproc::stop_grpc_server(&handle);
    bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
    if (l2_cache != nullptr) {
        bytetaper::cache::l2_close(&l2_cache);
    }
    bytetaper::observability::logger_shutdown();
    return 0;
}

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/inflight_registry.h"
#include "extproc/grpc_server.h"
#include "metrics/metrics_http_server.h"
#include "metrics/prometheus_registry.h"
#include "policy/yaml_loader.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
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
            args.metrics_port = static_cast<std::uint16_t>(std::atoi(argv[i + 1]));
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
    ServerArgs args = parse_args(argc, argv);
    if (args.error) {
        return 2;
    }
    if (args.help) {
        std::puts("usage: bytetaper-extproc-server [--listen-address HOST:PORT] [--policy-file "
                  "PATH] [--l2-cache-path PATH] [--metrics-address ADDR] [--metrics-port PORT]");
        return 0;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    bytetaper::policy::PolicyFileResult policy_result{};
    if (args.policy_file != nullptr) {
        if (!bytetaper::policy::load_policy_from_file(args.policy_file, &policy_result)) {
            std::fprintf(stderr, "failed to load policy file %s: %s\n", args.policy_file,
                         policy_result.error ? policy_result.error : "unknown error");
            return 3;
        }
        std::printf("loaded %zu routes from %s\n", policy_result.count, args.policy_file);
    }

    auto l1_cache = std::make_unique<bytetaper::cache::L1Cache>();
    bytetaper::cache::l1_init(l1_cache.get());

    bytetaper::cache::L2DiskCache* l2_cache = nullptr;
    if (args.l2_cache_path != nullptr) {
        l2_cache = bytetaper::cache::l2_open(args.l2_cache_path);
        if (l2_cache == nullptr) {
            std::fprintf(stderr, "failed to open L2 cache at %s\n", args.l2_cache_path);
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

    bytetaper::extproc::GrpcServerConfig config{};
    config.listen_address = args.listen_address;
    config.l1_cache = l1_cache.get();
    config.l2_cache = l2_cache;
    config.metrics_registry = &metrics_registry;
    config.coalescing_registry = coalescing_registry.get();
    if (policy_result.ok) {
        config.policies = policy_result.policies;
        config.policy_count = policy_result.count;
    }

    bytetaper::extproc::GrpcServerHandle handle{};
    if (!bytetaper::extproc::start_grpc_server(config, &handle)) {
        std::fprintf(stderr, "failed to start extproc server on %s\n", args.listen_address);
        if (l2_cache != nullptr) {
            bytetaper::cache::l2_close(&l2_cache);
        }
        return 1;
    }

    std::printf("bytetaper-extproc-server listening on %s (L1 enabled, L2 %s)\n",
                args.listen_address, l2_cache ? "enabled" : "disabled");
    std::printf("metrics server listening on %s:%u\n", args.metrics_listen_address,
                metrics_handle.bound_port);

    while (!g_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    bytetaper::extproc::stop_grpc_server(&handle);
    bytetaper::metrics::stop_metrics_http_server(&metrics_handle);
    if (l2_cache != nullptr) {
        bytetaper::cache::l2_close(&l2_cache);
    }
    return 0;
}

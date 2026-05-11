// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_EXTPROC_GRPC_SERVER_H
#define BYTETAPER_EXTPROC_GRPC_SERVER_H

#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "metrics/prometheus_registry.h"
#include "policy/route_policy.h"
#include "runtime/worker_queue.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::coalescing {
struct InFlightRegistry;
}

namespace bytetaper::extproc {

struct GrpcServerConfig {
    const char* listen_address = "127.0.0.1:0";
    const policy::RoutePolicy* policies = nullptr;
    std::size_t policy_count = 0;
    cache::L1Cache* l1_cache = nullptr;
    cache::L2DiskCache* l2_cache = nullptr;
    metrics::MetricsRegistry* metrics_registry = nullptr;
    coalescing::InFlightRegistry* coalescing_registry = nullptr;
    runtime::WorkerQueueConfig wq_config{};
};

struct GrpcServerHandle {
    void* impl = nullptr;
    std::uint16_t bound_port = 0;
    const char* startup_error = nullptr;
    runtime::WorkerQueueConfig effective_wq_config{};
};

bool start_grpc_server(const GrpcServerConfig& config, GrpcServerHandle* handle);
void stop_grpc_server(GrpcServerHandle* handle);

} // namespace bytetaper::extproc

#endif // BYTETAPER_EXTPROC_GRPC_SERVER_H

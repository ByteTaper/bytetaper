// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/grpc_server.h"
#include "runtime/worker_queue.h"

#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

// Internal class extracted from grpc_server.cpp for testing
namespace bytetaper::extproc {
class WorkerQueueStartGuard {
public:
    WorkerQueueStartGuard() = default;
    ~WorkerQueueStartGuard() {
        if (active_queue_ != nullptr) {
            runtime::worker_queue_shutdown(active_queue_);
        }
    }

    void arm(runtime::WorkerQueue* queue) {
        active_queue_ = queue;
    }
    void release() {
        active_queue_ = nullptr;
    }

private:
    runtime::WorkerQueue* active_queue_ = nullptr;
};
} // namespace bytetaper::extproc

namespace bytetaper::extproc {

TEST(GrpcServerWorkerLifecycle, WorkerQueueShutdownOnStartupFailure) {
    auto queue = std::make_unique<runtime::WorkerQueue>();
    runtime::WorkerQueueConfig config{};
    config.worker_count = 1;

    runtime::worker_queue_init(queue.get(), config);

    {
        WorkerQueueStartGuard guard{};
        runtime::worker_queue_start(queue.get(), {});
        guard.arm(queue.get());
    }

    EXPECT_FALSE(queue->running);
    EXPECT_FALSE(queue->workers[0].joinable());
}

TEST(GrpcServerWorkerLifecycle, RaiiGuardShutdownOnPartialStartupFailure) {
    auto queue = std::make_unique<runtime::WorkerQueue>();
    runtime::WorkerQueueConfig config{};
    config.worker_count = 1;
    runtime::worker_queue_init(queue.get(), config);

    {
        WorkerQueueStartGuard guard{};
        runtime::worker_queue_start(queue.get(), {});
        guard.arm(queue.get());
    }

    EXPECT_FALSE(queue->running);
}

TEST(GrpcServerWorkerLifecycle, RaiiGuardReleasesOnSuccess) {
    auto queue = std::make_unique<runtime::WorkerQueue>();
    runtime::WorkerQueueConfig config{};
    config.worker_count = 1;
    runtime::worker_queue_init(queue.get(), config);

    {
        WorkerQueueStartGuard guard{};
        runtime::worker_queue_start(queue.get(), {});
        guard.arm(queue.get());
        guard.release();
    }

    EXPECT_TRUE(queue->running);
    EXPECT_TRUE(queue->workers[0].joinable());

    runtime::worker_queue_shutdown(queue.get());
}

TEST(GrpcServerWorkerLifecycle, NoDoubleShutdownOnNormalStop) {
    auto queue = std::make_unique<runtime::WorkerQueue>();
    runtime::WorkerQueueConfig config{};
    config.worker_count = 1;
    runtime::worker_queue_init(queue.get(), config);
    runtime::worker_queue_start(queue.get(), {});

    runtime::worker_queue_shutdown(queue.get());
    EXPECT_FALSE(queue->running);
    EXPECT_FALSE(queue->workers[0].joinable());

    runtime::worker_queue_shutdown(queue.get());
    EXPECT_FALSE(queue->running);
    EXPECT_FALSE(queue->workers[0].joinable());
}

TEST(GrpcServerWorkerLifecycle, WorkerCountExceedsMaxFails) {
    GrpcServerConfig config{};
    config.listen_address = "127.0.0.1:0";
    config.wq_config.worker_count = runtime::kWorkerQueueMaxWorkers + 1;

    GrpcServerHandle handle{};
    EXPECT_FALSE(start_grpc_server(config, &handle));
    EXPECT_EQ(handle.impl, nullptr);
    ASSERT_NE(handle.startup_error, nullptr);
    EXPECT_STREQ(handle.startup_error, "invalid worker_count");
}

TEST(GrpcServerWorkerLifecycle, AsyncStoreMaxBodySizeExceedsAbsoluteCapFails) {
    GrpcServerConfig config{};
    config.listen_address = "127.0.0.1:0";
    config.wq_config.async_store_max_body_size = runtime::kAsyncL2StoreAbsoluteMaxBodySize + 1;

    GrpcServerHandle handle{};
    EXPECT_FALSE(start_grpc_server(config, &handle));
    EXPECT_EQ(handle.impl, nullptr);
    ASSERT_NE(handle.startup_error, nullptr);
    EXPECT_STREQ(handle.startup_error,
                 "worker_queue: async_store_max_body_size exceeds absolute cap");
}

TEST(GrpcServerWorkerLifecycle, ValidCustomConfigStartsSuccessfully) {
    GrpcServerConfig config{};
    config.listen_address = "127.0.0.1:0";
    config.wq_config.worker_count = 3;
    config.wq_config.lookup_lane_quota = 5;
    config.wq_config.store_lane_quota = 2;
    config.wq_config.async_store_max_body_size = 12345;

    GrpcServerHandle handle{};
    EXPECT_TRUE(start_grpc_server(config, &handle));
    EXPECT_NE(handle.impl, nullptr);
    EXPECT_EQ(handle.effective_wq_config.worker_count, 3u);
    EXPECT_EQ(handle.effective_wq_config.lookup_lane_quota, 5u);
    EXPECT_EQ(handle.effective_wq_config.store_lane_quota, 2u);
    EXPECT_EQ(handle.effective_wq_config.async_store_max_body_size, 12345u);
    EXPECT_EQ(handle.startup_error, nullptr);

    stop_grpc_server(&handle);
}

TEST(GrpcServerWorkerLifecycle, DerivedBodySizeFromPolicyRoute) {
    policy::RoutePolicy policy{};
    policy.max_response_bytes = 54321;

    GrpcServerConfig config{};
    config.listen_address = "127.0.0.1:0";
    config.policies = &policy;
    config.policy_count = 1;
    config.wq_config.async_store_max_body_size = 0; // Trigger derivation

    GrpcServerHandle handle{};
    EXPECT_TRUE(start_grpc_server(config, &handle));
    EXPECT_NE(handle.impl, nullptr);
    EXPECT_EQ(handle.effective_wq_config.async_store_max_body_size, 54321u);
    EXPECT_EQ(handle.startup_error, nullptr);

    stop_grpc_server(&handle);
}

} // namespace bytetaper::extproc

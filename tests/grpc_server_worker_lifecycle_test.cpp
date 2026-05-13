// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "extproc/grpc_server.h"
#include "runtime/worker_queue.h"

#include <atomic>
#include <grpcpp/grpcpp.h>
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

TEST(GrpcServerWorkerLifecycle, StreamFilterStateSnapshotRetention) {
    // 1. Create a RuntimePolicyStore on the heap and seed it with a snapshot
    auto store = std::make_unique<runtime::RuntimePolicyStore>();

    policy::RoutePolicy initial_routes[1]{};
    initial_routes[0].route_id = "initial-policy-id";
    initial_routes[0].match_prefix = "/api/";
    initial_routes[0].match_kind = policy::RouteMatchKind::Prefix;
    initial_routes[0].mutation = policy::MutationMode::Full;

    auto build_res1 =
        runtime::build_runtime_policy_snapshot_from_routes(initial_routes, 1, "test.yaml", 1);
    ASSERT_TRUE(build_res1.ok);
    std::string err;
    ASSERT_TRUE(store->install_initial(build_res1.snapshot, &err));

    // 2. Start the gRPC server using this store
    GrpcServerConfig config{};
    config.listen_address = "127.0.0.1:0";
    config.policy_store = store.get();

    GrpcServerHandle handle{};
    ASSERT_TRUE(start_grpc_server(config, &handle));
    ASSERT_NE(handle.impl, nullptr);
    ASSERT_NE(handle.bound_port, 0);

    // 3. Connect a gRPC client channel and open a stream
    const std::string target =
        "127.0.0.1:" + std::to_string(static_cast<std::uint32_t>(handle.bound_port));
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    ASSERT_NE(channel, nullptr);

    const auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(2);
    ASSERT_TRUE(channel->WaitForConnected(deadline));

    auto stub = envoy::service::ext_proc::v3::ExternalProcessor::NewStub(channel);
    ASSERT_NE(stub, nullptr);

    grpc::ClientContext client_context{};
    auto stream = stub->Process(&client_context);
    ASSERT_NE(stream, nullptr);

    // 4. Send RequestHeaders (which triggers snapshot loading inside StreamFilterState)
    envoy::service::ext_proc::v3::ProcessingRequest request_headers{};
    auto* req_headers_field = request_headers.mutable_request_headers();
    auto* r_headers = req_headers_field->mutable_headers();
    auto* h1 = r_headers->add_headers();
    h1->set_key(":path");
    h1->set_value("/api/v1/resource");

    ASSERT_TRUE(stream->Write(request_headers));

    // Wait and read Response 1 (from request_headers) to guarantee the server has
    // processed the RequestHeaders branch and pinned Snapshot A before we swap!
    envoy::service::ext_proc::v3::ProcessingResponse resp1{};
    ASSERT_TRUE(stream->Read(&resp1));
    EXPECT_TRUE(resp1.has_request_headers());

    // 5. While the stream is active, hot-swap the active snapshot in the policy store!
    policy::RoutePolicy new_routes[1]{};
    new_routes[0].route_id = "completely-new-policy-id";
    new_routes[0].match_prefix = "/api/";
    new_routes[0].match_kind = policy::RouteMatchKind::Prefix;
    new_routes[0].mutation = policy::MutationMode::Full;

    auto build_res2 =
        runtime::build_runtime_policy_snapshot_from_routes(new_routes, 1, "test.yaml", 2);
    ASSERT_TRUE(build_res2.ok);
    ASSERT_TRUE(store->swap(build_res2.snapshot, &err));

    // 6. Write ResponseHeaders on the SAME active stream
    envoy::service::ext_proc::v3::ProcessingRequest response_headers{};
    response_headers.mutable_response_headers();
    ASSERT_TRUE(stream->Write(response_headers));

    // Write WritesDone
    ASSERT_TRUE(stream->WritesDone());

    // 7. Verify the responses returned by the stream.
    // Response 2: from response_headers
    envoy::service::ext_proc::v3::ProcessingResponse resp2{};
    ASSERT_TRUE(stream->Read(&resp2));
    EXPECT_TRUE(resp2.has_response_headers());

    // Proves snapshot-ownership and reference-pinning safety:
    // Assert that the active stream processed its headers using Snapshot A (initial-policy-id),
    // even though the store was hot-swapped to Snapshot B (completely-new-policy-id) prior to
    // response headers!
    bool found_route_id = false;
    std::string route_id_value = "";
    for (const auto& mutation :
         resp2.response_headers().response().header_mutation().set_headers()) {
        if (mutation.header().key() == "x-bytetaper-route-policy") {
            found_route_id = true;
            route_id_value = mutation.header().raw_value();
            break;
        }
    }
    EXPECT_TRUE(found_route_id);
    EXPECT_EQ(route_id_value, "initial-policy-id");

    // The stream should close successfully
    grpc::Status status = stream->Finish();
    EXPECT_TRUE(status.ok());

    // 8. Open a new stream post-swap to verify it uses the new snapshot B!
    grpc::ClientContext client_context2{};
    auto stream2 = stub->Process(&client_context2);
    ASSERT_NE(stream2, nullptr);

    envoy::service::ext_proc::v3::ProcessingRequest request_headers2{};
    auto* req_headers_field2 = request_headers2.mutable_request_headers();
    auto* r_headers2 = req_headers_field2->mutable_headers();
    auto* h2 = r_headers2->add_headers();
    h2->set_key(":path");
    h2->set_value("/api/v1/resource");

    ASSERT_TRUE(stream2->Write(request_headers2));

    envoy::service::ext_proc::v3::ProcessingRequest response_headers2{};
    response_headers2.mutable_response_headers();
    ASSERT_TRUE(stream2->Write(response_headers2));
    ASSERT_TRUE(stream2->WritesDone());

    envoy::service::ext_proc::v3::ProcessingResponse resp1_s2{};
    ASSERT_TRUE(stream2->Read(&resp1_s2));

    envoy::service::ext_proc::v3::ProcessingResponse resp2_s2{};
    ASSERT_TRUE(stream2->Read(&resp2_s2));
    EXPECT_TRUE(resp2_s2.has_response_headers());

    bool found_route_id2 = false;
    std::string route_id_value2 = "";
    for (const auto& mutation :
         resp2_s2.response_headers().response().header_mutation().set_headers()) {
        if (mutation.header().key() == "x-bytetaper-route-policy") {
            found_route_id2 = true;
            route_id_value2 = mutation.header().raw_value();
            break;
        }
    }
    EXPECT_TRUE(found_route_id2);
    EXPECT_EQ(route_id_value2, "completely-new-policy-id");

    EXPECT_TRUE(stream2->Finish().ok());

    stop_grpc_server(&handle);
}

} // namespace bytetaper::extproc

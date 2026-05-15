// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "cache/cache_key.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "extproc/grpc_server.h"
#include "policy/policy_identity.h"
#include "policy/route_policy.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <vector>

using namespace bytetaper;

int main() {
    // 1. Setup Policy
    policy::RoutePolicy policy{};
    policy.route_id = "test-route";
    policy.match_kind = policy::RouteMatchKind::Prefix;
    policy.match_prefix = "/";
    policy.cache.behavior = policy::CacheBehavior::Store;
    policy::copy_route_policy_identity_v2_to_legacy_slot(&policy);

    // 2. Setup Caches
    auto l1_cache = std::make_unique<cache::L1Cache>();
    cache::l1_init(l1_cache.get());

    const char* db_path = "/tmp/bt_l2_sync_removed_test";
    cache::l2_destroy(db_path);
    auto* l2_cache = cache::l2_open(db_path);
    if (!l2_cache)
        return 1;

    // 3. Start Server
    extproc::GrpcServerConfig config{};
    config.policies = &policy;
    config.policy_count = 1;
    config.l1_cache = l1_cache.get();
    config.l2_cache = l2_cache;

    extproc::GrpcServerHandle handle{};
    if (!extproc::start_grpc_server(config, &handle)) {
        cache::l2_close(&l2_cache);
        return 2;
    }

    const std::string target = "127.0.0.1:" + std::to_string(handle.bound_port);
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = envoy::service::ext_proc::v3::ExternalProcessor::NewStub(channel);

    // Scenario A: L1 Miss, L2 Hit (Seeded) -> Should result in upstream call, NOT L2 hit.
    {
        // Seed L2
        cache::CacheKeyInput ki{};
        ki.method = policy::HttpMethod::Get;
        ki.route_id = "test-route";
        ki.path = "/scenario-a";
        ki.policy_version = policy.policy_identity;
        ki.route_cache_epoch = 1;
        ki.route_cache_epoch_ready = true;

        char key[cache::kCacheKeyMaxLen] = {};
        cache::build_cache_key(ki, key, sizeof(key));

        cache::CacheEntry entry{};
        std::strcpy(entry.key, key);
        entry.status_code = 200;
        entry.body = "L2-CACHED-BODY";
        entry.body_len = 14;
        entry.expires_at_epoch_ms = 0; // never expires
        cache::l2_put(l2_cache, entry);

        // Send RequestHeaders
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
        auto stream = stub->Process(&ctx);

        envoy::service::ext_proc::v3::ProcessingRequest req;
        auto* hdr_map = req.mutable_request_headers()->mutable_headers();

        auto* m_hdr = hdr_map->add_headers();
        m_hdr->set_key(":method");
        m_hdr->set_raw_value("GET");

        auto* p_hdr = hdr_map->add_headers();
        p_hdr->set_key(":path");
        p_hdr->set_raw_value("/scenario-a");

        if (!stream->Write(req)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 9;
        }

        // Read Response
        envoy::service::ext_proc::v3::ProcessingResponse resp;
        if (!stream->Read(&resp)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 11;
        }

        // ASSERT: No immediate response (L2 lookup was skipped)
        if (resp.has_immediate_response()) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 10;
        }

        // Send ResponseHeaders + ResponseBody (Mock Upstream)
        req.Clear();
        auto* rsh_map = req.mutable_response_headers()->mutable_headers();
        auto* s_hdr = rsh_map->add_headers();
        s_hdr->set_key(":status");
        s_hdr->set_raw_value("200");
        if (!stream->Write(req)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 12;
        }

        req.Clear();
        auto* rb = req.mutable_response_body();
        rb->set_body("UPSTREAM-BODY");
        rb->set_end_of_stream(true);
        if (!stream->Write(req)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 13;
        }

        stream->WritesDone();

        // Read ResponseHeaders response
        if (!stream->Read(&resp)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 14;
        }
        // Read ResponseBody response
        if (!stream->Read(&resp)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 15;
        }

        stream->Finish();
    }

    // Scenario B: L1 Hit (Seeded) -> Should still short-circuit.
    {
        // Seed L1
        cache::CacheKeyInput ki{};
        ki.method = policy::HttpMethod::Get;
        ki.route_id = "test-route";
        ki.path = "/scenario-b";
        ki.policy_version = policy.policy_identity;
        ki.route_cache_epoch = 1;
        ki.route_cache_epoch_ready = true;

        char key[cache::kCacheKeyMaxLen] = {};
        cache::build_cache_key(ki, key, sizeof(key));

        cache::CacheEntry entry{};
        std::strcpy(entry.key, key);
        entry.status_code = 200;
        entry.body = "L1-CACHED-BODY";
        entry.body_len = 14;
        entry.expires_at_epoch_ms = 0;
        cache::l1_put(l1_cache.get(), entry);

        // Send RequestHeaders
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
        auto stream = stub->Process(&ctx);

        envoy::service::ext_proc::v3::ProcessingRequest req;
        auto* hdr_map = req.mutable_request_headers()->mutable_headers();

        auto* m_hdr = hdr_map->add_headers();
        m_hdr->set_key(":method");
        m_hdr->set_raw_value("GET");

        auto* p_hdr = hdr_map->add_headers();
        p_hdr->set_key(":path");
        p_hdr->set_raw_value("/scenario-b");

        if (!stream->Write(req)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 19;
        }

        // Read Response
        envoy::service::ext_proc::v3::ProcessingResponse resp;
        if (!stream->Read(&resp)) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 22;
        }

        // ASSERT: Immediate response (L1 hit)
        if (!resp.has_immediate_response()) {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 20;
        }

        if (resp.immediate_response().body() != "L1-CACHED-BODY") {
            extproc::stop_grpc_server(&handle);
            cache::l2_close(&l2_cache);
            return 21;
        }

        stream->WritesDone();
        stream->Finish();
    }

    extproc::stop_grpc_server(&handle);
    cache::l2_close(&l2_cache);
    cache::l2_destroy(db_path);

    return 0;
}

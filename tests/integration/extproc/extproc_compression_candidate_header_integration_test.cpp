// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "extproc/grpc_server.h"
#include "policy/route_policy.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <grpcpp/grpcpp.h>
#include <string>

static bytetaper::policy::RoutePolicy make_compression_policy() {
    bytetaper::policy::RoutePolicy p{};
    p.route_id = "test-compression";
    p.match_kind = bytetaper::policy::RouteMatchKind::Prefix;
    p.match_prefix = "/api";
    p.mutation = bytetaper::policy::MutationMode::Full;
    p.compression.enabled = true;
    p.compression.min_size_bytes = 512;
    std::strncpy(p.compression.eligible_content_types[0], "application/json", 63);
    p.compression.eligible_content_type_count = 1;
    return p;
}

static bool find_body_header(const envoy::service::ext_proc::v3::ProcessingResponse& resp,
                             const std::string& key, const std::string& expected_value) {
    if (!resp.has_response_body())
        return false;
    for (const auto& m : resp.response_body().response().header_mutation().set_headers()) {
        if (m.header().key() == key && m.header().raw_value() == expected_value) {
            return true;
        }
    }
    return false;
}

static bool find_headers_header(const envoy::service::ext_proc::v3::ProcessingResponse& resp,
                                const std::string& key, const std::string& expected_value) {
    if (!resp.has_response_headers())
        return false;
    for (const auto& m : resp.response_headers().response().header_mutation().set_headers()) {
        if (m.header().key() == key && m.header().raw_value() == expected_value) {
            return true;
        }
    }
    return false;
}

static bool has_any_body_header(const envoy::service::ext_proc::v3::ProcessingResponse& resp,
                                const std::string& key) {
    if (!resp.has_response_body())
        return false;
    for (const auto& m : resp.response_body().response().header_mutation().set_headers()) {
        if (m.header().key() == key) {
            return true;
        }
    }
    return false;
}

static bool has_any_headers_header(const envoy::service::ext_proc::v3::ProcessingResponse& resp,
                                   const std::string& key) {
    if (!resp.has_response_headers())
        return false;
    for (const auto& m : resp.response_headers().response().header_mutation().set_headers()) {
        if (m.header().key() == key) {
            return true;
        }
    }
    return false;
}

int main() {
    bytetaper::policy::RoutePolicy policies[2];
    policies[0] = make_compression_policy();

    policies[1] = make_compression_policy();
    policies[1].route_id = "test-full-diagnostics";
    policies[1].match_prefix = "/full-diag";
    policies[1].cache.behavior = bytetaper::policy::CacheBehavior::Store;

    bytetaper::extproc::GrpcServerConfig config{};
    config.policies = policies;
    config.policy_count = 2;
    config.listen_address = "127.0.0.1:0";
    bytetaper::extproc::GrpcServerHandle handle{};

    if (!bytetaper::extproc::start_grpc_server(config, &handle))
        return 1;
    if (handle.bound_port == 0) {
        bytetaper::extproc::stop_grpc_server(&handle);
        return 2;
    }

    const std::string target = "127.0.0.1:" + std::to_string(handle.bound_port);
    const auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    if (!channel) {
        bytetaper::extproc::stop_grpc_server(&handle);
        return 3;
    }

    const auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    if (!channel->WaitForConnected(deadline)) {
        bytetaper::extproc::stop_grpc_server(&handle);
        return 4;
    }

    auto stub = envoy::service::ext_proc::v3::ExternalProcessor::NewStub(channel);

    // ── Scenario A: Eligible ──────────────────────────────────────────
    {
        grpc::ClientContext ctx{};
        auto stream = stub->Process(&ctx);

        // Request Headers
        envoy::service::ext_proc::v3::ProcessingRequest req_hdrs{};
        auto* rh = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        rh->set_key(":path");
        rh->set_raw_value("/api/data");
        auto* rm = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        rm->set_key(":method");
        rm->set_raw_value("GET");
        auto* ae = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        ae->set_key("accept-encoding");
        ae->set_raw_value("br, gzip");
        stream->Write(req_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r1{};
        stream->Read(&r1);

        // Response Headers
        envoy::service::ext_proc::v3::ProcessingRequest resp_hdrs{};
        auto* rs = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        rs->set_key(":status");
        rs->set_raw_value("200");
        auto* ct = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        ct->set_key("content-type");
        ct->set_raw_value("application/json");
        stream->Write(resp_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r2{};
        stream->Read(&r2);

        // Response Body
        envoy::service::ext_proc::v3::ProcessingRequest resp_body{};
        std::string large_body(1024, 'a');
        resp_body.mutable_response_body()->set_body(large_body);
        resp_body.mutable_response_body()->set_end_of_stream(true);
        stream->Write(resp_body);
        envoy::service::ext_proc::v3::ProcessingResponse r3{};
        stream->Read(&r3);

        if (!find_body_header(r3, "x-bytetaper-compression-candidate", "true"))
            return 100;
        if (!find_body_header(r3, "x-bytetaper-compression-algorithm-hint", "br"))
            return 101;

        stream->WritesDone();
        stream->Finish();
    }

    // ── Scenario B: Ineligible (Content-Type) ──────────────────────────
    {
        grpc::ClientContext ctx{};
        auto stream = stub->Process(&ctx);

        // Request Headers
        envoy::service::ext_proc::v3::ProcessingRequest req_hdrs{};
        auto* rh = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        rh->set_key(":path");
        rh->set_raw_value("/api/image");
        auto* rm = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        rm->set_key(":method");
        rm->set_raw_value("GET");
        auto* ae = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        ae->set_key("accept-encoding");
        ae->set_raw_value("br, gzip");
        stream->Write(req_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r1{};
        stream->Read(&r1);

        // Response Headers
        envoy::service::ext_proc::v3::ProcessingRequest resp_hdrs{};
        auto* rs = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        rs->set_key(":status");
        rs->set_raw_value("200");
        auto* ct = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        ct->set_key("content-type");
        ct->set_raw_value("image/png");
        stream->Write(resp_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r2{};
        stream->Read(&r2);

        // Response Body
        envoy::service::ext_proc::v3::ProcessingRequest resp_body{};
        resp_body.mutable_response_body()->set_body("fake-image-data");
        resp_body.mutable_response_body()->set_end_of_stream(true);
        stream->Write(resp_body);
        envoy::service::ext_proc::v3::ProcessingResponse r3{};
        stream->Read(&r3);

        if (!find_body_header(r3, "x-bytetaper-compression-candidate", "false"))
            return 200;
        if (!find_body_header(r3, "x-bytetaper-compression-reason", "content_type_not_eligible"))
            return 201;

        stream->WritesDone();
        stream->Finish();
    }

    // ── Scenario C: Eligible with Content-Length (Header-Only Decision) ──
    {
        grpc::ClientContext ctx{};
        auto stream = stub->Process(&ctx);

        // Request Headers
        envoy::service::ext_proc::v3::ProcessingRequest req_hdrs{};
        auto* rh = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        rh->set_key(":path");
        rh->set_raw_value("/api/data");
        auto* rm = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        rm->set_key(":method");
        rm->set_raw_value("GET");
        auto* ae = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        ae->set_key("accept-encoding");
        ae->set_raw_value("br, gzip");
        stream->Write(req_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r1{};
        stream->Read(&r1);

        // Response Headers with content-length present
        envoy::service::ext_proc::v3::ProcessingRequest resp_hdrs{};
        auto* rs = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        rs->set_key(":status");
        rs->set_raw_value("200");
        auto* ct = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        ct->set_key("content-type");
        ct->set_raw_value("application/json");
        auto* cl = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        cl->set_key("content-length");
        cl->set_raw_value("1024");
        stream->Write(resp_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r2{};
        stream->Read(&r2);

        // Verify that compression candidate and algorithm hint headers are emitted EARLY in
        // response headers!
        if (!find_headers_header(r2, "x-bytetaper-compression-candidate", "true"))
            return 300;
        if (!find_headers_header(r2, "x-bytetaper-compression-algorithm-hint", "br"))
            return 301;

        // Since the route is compression-only, it must NOT emit any bytetaper report headers
        // in ResponseHeaders phase.
        if (has_any_headers_header(r2, "x-bytetaper-extproc-response-body"))
            return 305;
        if (has_any_headers_header(r2, "x-bytetaper-route-policy"))
            return 306;

        // Response Body
        envoy::service::ext_proc::v3::ProcessingRequest resp_body{};
        std::string large_body(1024, 'a');
        resp_body.mutable_response_body()->set_body(large_body);
        resp_body.mutable_response_body()->set_end_of_stream(true);
        stream->Write(resp_body);
        envoy::service::ext_proc::v3::ProcessingResponse r3{};
        stream->Read(&r3);

        // Since the decision was final, the body stage must NOT have re-evaluated or written
        // duplicate compression headers.
        if (find_body_header(r3, "x-bytetaper-compression-candidate", "true"))
            return 302;

        // Since the decision was final and body processing is not needed, body stage must NOT
        // have written any bytetaper report headers.
        if (has_any_body_header(r3, "x-bytetaper-extproc-response-body"))
            return 303;
        if (has_any_body_header(r3, "x-bytetaper-original-bytes"))
            return 304;

        stream->WritesDone();
        stream->Finish();
    }

    // Scenario D: Full Diagnostics Route (matching /full-diag)
    {
        grpc::ClientContext client_context{};
        auto stream = stub->Process(&client_context);
        if (!stream) {
            bytetaper::extproc::stop_grpc_server(&handle);
            return 400;
        }

        // Request Headers
        envoy::service::ext_proc::v3::ProcessingRequest req_hdrs{};
        auto* hp = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        hp->set_key(":path");
        hp->set_raw_value("/full-diag");
        auto* rm = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        rm->set_key(":method");
        rm->set_raw_value("GET");
        auto* ae = req_hdrs.mutable_request_headers()->mutable_headers()->add_headers();
        ae->set_key("accept-encoding");
        ae->set_raw_value("br, gzip");
        stream->Write(req_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r1{};
        stream->Read(&r1);

        // Response Headers with content-length present
        envoy::service::ext_proc::v3::ProcessingRequest resp_hdrs{};
        auto* rs = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        rs->set_key(":status");
        rs->set_raw_value("200");
        auto* ct = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        ct->set_key("content-type");
        ct->set_raw_value("application/json");
        auto* cl = resp_hdrs.mutable_response_headers()->mutable_headers()->add_headers();
        cl->set_key("content-length");
        cl->set_raw_value("1024");
        stream->Write(resp_hdrs);
        envoy::service::ext_proc::v3::ProcessingResponse r2{};
        stream->Read(&r2);

        // Since this is a FullDiagnostics route, it MUST emit report headers in ResponseHeaders
        // phase!
        if (!find_headers_header(r2, "x-bytetaper-extproc-response-body", "true"))
            return 401;
        if (!find_headers_header(r2, "x-bytetaper-route-policy", "test-full-diagnostics"))
            return 402;

        stream->WritesDone();
        stream->Finish();
    }

    bytetaper::extproc::stop_grpc_server(&handle);
    return 0;
}

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "admin/taperquery_admin_http_server.h"
#include "runtime/policy_snapshot.h"
#include "taperquery/policy_ir.h"
#include "taperquery/tq_apply_audit.h"
#include "taperquery/tq_apply_service.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace bytetaper::admin::testing {

namespace {

taperquery::TqPolicyDocument create_valid_document(const std::string& doc_id,
                                                   const std::string& base_sha,
                                                   const std::string& route_id,
                                                   const std::string& prefix) {
    taperquery::TqPolicyDocument doc;
    doc.document_id = doc_id;
    doc.version.source_schema_version = "tq/v1";
    doc.expected_base_sha = base_sha;

    taperquery::TqRoutePolicy r;
    r.route_id = route_id;
    r.match_prefix = prefix;
    r.match_kind = taperquery::TqRouteMatchKind::Prefix;
    r.mutation = taperquery::TqMutationMode::Disabled;
    r.allowed_method = taperquery::TqHttpMethod::Any;
    doc.routes.push_back(r);

    return doc;
}

static std::string escape_json(const std::string& input) {
    std::string out;
    for (char c : input) {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else
            out.push_back(c);
    }
    return out;
}

std::string send_http_request(std::uint16_t port, const std::string& method,
                              const std::string& path, const std::string& body = "",
                              const std::string& extra_headers = "") {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return "";

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return "";
    }

    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    if (!body.empty() || method == "POST") {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        request += "Content-Type: application/json\r\n";
    }
    request += extra_headers;
    request += "\r\n";
    request += body;

    send(sock, request.data(), request.size(), 0);

    std::string response;
    char buffer[1024];
    while (true) {
        ssize_t n = read(sock, buffer, sizeof(buffer));
        if (n <= 0)
            break;
        response.append(buffer, n);
    }
    close(sock);
    return response;
}

} // namespace

class TaperQueryAdminHttpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        store = std::make_unique<runtime::RuntimePolicyStore>();
        uint64_t initial_gen = store->next_generation();

        taperquery::TqPolicyDocument init_doc =
            create_valid_document("my-policy", "", "initial_route", "/initial");

        auto build_res = runtime::build_runtime_policy_snapshot_from_ir(init_doc, initial_gen);
        ASSERT_TRUE(build_res.ok) << build_res.error;
        ASSERT_NE(build_res.snapshot, nullptr);

        initial_identity = build_res.snapshot->policy_identity;

        std::string err;
        ASSERT_TRUE(store->install_initial(build_res.snapshot, &err)) << err;

        audit_store = std::make_unique<taperquery::TqApplyAuditStore>();
        apply_service =
            std::make_unique<taperquery::TqApplyService>(store.get(), nullptr, audit_store.get());

        config.listen_address = "127.0.0.1";
        config.port = 0; // OS-assigned port
        config.policy_store = store.get();
        config.apply_service = apply_service.get();
        config.audit_store = audit_store.get();
        config.enable_taperquery_apply = true;
        config.max_request_bytes = 4096;

        ASSERT_TRUE(start_taperquery_admin_http_server(config, &handle));
        ASSERT_NE(handle.bound_port, 0);
    }

    void TearDown() override {
        stop_taperquery_admin_http_server(&handle);
    }

    std::unique_ptr<runtime::RuntimePolicyStore> store;
    std::unique_ptr<taperquery::TqApplyAuditStore> audit_store;
    std::unique_ptr<taperquery::TqApplyService> apply_service;
    TaperQueryAdminHttpServerConfig config;
    TaperQueryAdminHttpServerHandle handle{};
    std::string initial_identity;
};

TEST_F(TaperQueryAdminHttpServerTest, GetCurrentPolicySuccess) {
    std::string resp =
        send_http_request(handle.bound_port, "GET", "/admin/taperquery/policy/current");
    EXPECT_NE(resp.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(resp.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(resp.find("\"policy_identity\":\"" + initial_identity + "\""), std::string::npos);
    EXPECT_NE(resp.find("\"generation\":1"), std::string::npos);
    EXPECT_NE(resp.find("\"route_count\":1"), std::string::npos);
}

TEST_F(TaperQueryAdminHttpServerTest, GetCurrentPolicyNoActive) {
    auto empty_store = std::make_unique<runtime::RuntimePolicyStore>();
    TaperQueryAdminHttpServerConfig local_config;
    local_config.listen_address = "127.0.0.1";
    local_config.port = 0;
    local_config.policy_store = empty_store.get();

    TaperQueryAdminHttpServerHandle local_handle{};
    ASSERT_TRUE(start_taperquery_admin_http_server(local_config, &local_handle));

    std::string resp =
        send_http_request(local_handle.bound_port, "GET", "/admin/taperquery/policy/current");
    EXPECT_NE(resp.find("HTTP/1.1 503 Service Unavailable"), std::string::npos);
    EXPECT_NE(resp.find("NO_ACTIVE_POLICY"), std::string::npos);

    stop_taperquery_admin_http_server(&local_handle);
}

TEST_F(TaperQueryAdminHttpServerTest, ApplyDisabled) {
    // Stop default server and start one with apply disabled
    stop_taperquery_admin_http_server(&handle);
    config.enable_taperquery_apply = false;
    ASSERT_TRUE(start_taperquery_admin_http_server(config, &handle));

    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"policy \\\"my-policy\\\" against sha \\\"" +
                       initial_identity +
                       "\\\" { route \\\"r1\\\" when path prefix \\\"/p1\\\" {} }\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"dry_run\"\n"
                       "}";

    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp.find("HTTP/1.1 403 Forbidden"), std::string::npos);
    EXPECT_NE(resp.find("FORBIDDEN"), std::string::npos);
}

TEST_F(TaperQueryAdminHttpServerTest, ApplyDryRunSuccess) {
    auto pre_snap = store->load();
    std::string pre_identity = pre_snap->policy_identity;
    std::uint32_t pre_generation = pre_snap->generation;

    std::string policy_src = "policy \"my-policy\" against sha \"" + initial_identity +
                             "\" { route \"r1\" when path prefix \"/p\" {} }";
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"" +
                       escape_json(policy_src) +
                       "\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"dry_run\"\n"
                       "}";

    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(resp.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(resp.find("\"status\":\"DryRunReady\""), std::string::npos);

    // Verify policy store has not mutated during dry-run
    auto post_snap = store->load();
    EXPECT_EQ(post_snap->policy_identity, pre_identity);
    EXPECT_EQ(post_snap->generation, pre_generation);
}

TEST_F(TaperQueryAdminHttpServerTest, ApplyRealCommitSuccess) {
    std::string policy_src = "policy \"my-policy\" against sha \"" + initial_identity +
                             "\" { route \"r1\" when path prefix \"/p\" {} }";
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"" +
                       escape_json(policy_src) +
                       "\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"apply\"\n"
                       "}";

    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(resp.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(resp.find("\"status\":\"Applied\""), std::string::npos);
    EXPECT_NE(resp.find("\"after_generation\":2"), std::string::npos);

    // Verify policy store updated
    auto post_snap = store->load();
    EXPECT_EQ(post_snap->generation, 2u);
    EXPECT_NE(post_snap->policy_identity, initial_identity);

    // Verify response agrees with the updated active snapshot identity
    EXPECT_NE(resp.find("\"applied_policy_identity\":\"" + post_snap->policy_identity + "\""),
              std::string::npos);
}

TEST_F(TaperQueryAdminHttpServerTest, ApplyCasMismatch) {
    std::string policy_src =
        "policy \"my-policy\" against sha \"bad-sha\" { route \"r1\" when path prefix \"/p\" {} }";
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"" +
                       escape_json(policy_src) +
                       "\",\n"
                       "  \"expected_base_identity\": \"bad-sha\",\n"
                       "  \"mode\": \"apply\"\n"
                       "}";

    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp.find("HTTP/1.1 409 Conflict"), std::string::npos);
    EXPECT_NE(resp.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(resp.find("\"status\":\"RejectedCasMismatch\""), std::string::npos);
    EXPECT_NE(resp.find("\"error_code\":\"CAS_MISMATCH\""), std::string::npos);
    EXPECT_NE(resp.find("\"current_policy_identity\":\"" + initial_identity + "\""),
              std::string::npos);
}

TEST_F(TaperQueryAdminHttpServerTest, InvalidRequests) {
    // Missing fields (general)
    {
        std::string body = "{}";
        std::string resp =
            send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
        EXPECT_NE(resp.find("HTTP/1.1 400 Bad Request"), std::string::npos);
        EXPECT_NE(resp.find("INVALID_REQUEST"), std::string::npos);
        EXPECT_NE(resp.find("expected_base_identity"), std::string::npos);
    }

    // Missing expected_base_identity specifically
    {
        std::string body =
            R"({"source_type":"taperquery","source":"policy \"foo\" {}","mode":"dry_run"})";
        std::string resp =
            send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
        EXPECT_NE(resp.find("HTTP/1.1 400 Bad Request"), std::string::npos);
        EXPECT_NE(resp.find("INVALID_REQUEST"), std::string::npos);
        EXPECT_NE(resp.find("expected_base_identity"), std::string::npos);
    }

    // Unsupported source type
    {
        std::string body =
            R"({"source_type":"yaml","source":"foo","expected_base_identity":"something","mode":"dry_run"})";
        std::string resp =
            send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
        EXPECT_NE(resp.find("HTTP/1.1 400 Bad Request"), std::string::npos);
        EXPECT_NE(resp.find("Unsupported source_type"), std::string::npos);
    }

    // Payload too large
    {
        std::string huge_body(5000, 'A');
        std::string resp =
            send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", huge_body);
        EXPECT_NE(resp.find("HTTP/1.1 413 Payload Too Large"), std::string::npos);
    }

    // Method not allowed (GET current policy with POST)
    {
        std::string resp =
            send_http_request(handle.bound_port, "POST", "/admin/taperquery/policy/current");
        EXPECT_NE(resp.find("HTTP/1.1 405 Method Not Allowed"), std::string::npos);
    }

    // Method not allowed (POST apply with GET)
    {
        std::string resp = send_http_request(handle.bound_port, "GET", "/admin/taperquery/apply");
        EXPECT_NE(resp.find("HTTP/1.1 405 Method Not Allowed"), std::string::npos);
    }

    // Not found
    {
        std::string resp = send_http_request(handle.bound_port, "GET", "/unknown");
        EXPECT_NE(resp.find("HTTP/1.1 404 Not Found"), std::string::npos);
    }
}

TEST_F(TaperQueryAdminHttpServerTest, ApplyMalformedJson) {
    std::string malformed_body = R"({"source_type":"taperquery", "source": "policy)";
    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", malformed_body);
    EXPECT_NE(resp.find("HTTP/1.1 400 Bad Request"), std::string::npos);
    EXPECT_NE(resp.find("INVALID_REQUEST"), std::string::npos);

    // Verify policy store has not changed
    EXPECT_EQ(store->load()->policy_identity, initial_identity);
    EXPECT_EQ(store->load()->generation, 1u);
}

TEST_F(TaperQueryAdminHttpServerTest, ApplySemanticValidationError) {
    // Zero capacity for L1 is semantic validation failure
    std::string policy_src = "policy \"pid\" { route \"r1\" when path prefix \"/api\" { cache "
                             "store ttl 60s { l1 enabled capacity 0 entries } } }";
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"" +
                       escape_json(policy_src) +
                       "\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"apply\"\n"
                       "}";

    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp.find("HTTP/1.1 422 Unprocessable Entity"), std::string::npos);
    EXPECT_NE(resp.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(resp.find("\"status\":\"RejectedValidation\""), std::string::npos);
    EXPECT_NE(resp.find("\"error_code\":\"VALIDATION_ERROR\""), std::string::npos);

    // Verify policy store has not changed
    EXPECT_EQ(store->load()->policy_identity, initial_identity);
    EXPECT_EQ(store->load()->generation, 1u);
}

TEST_F(TaperQueryAdminHttpServerTest, ApplyRouteAnalysisError) {
    // Duplicate matches trigger ROUTE_ANALYSIS_ERROR
    std::string policy_src = "policy \"pid\" { route \"r1\" when path exact \"/api\" {} route "
                             "\"r2\" when path exact \"/api\" {} }";
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"" +
                       escape_json(policy_src) +
                       "\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"apply\"\n"
                       "}";

    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp.find("HTTP/1.1 422 Unprocessable Entity"), std::string::npos);
    EXPECT_NE(resp.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(resp.find("\"status\":\"RejectedRouteAnalysis\""), std::string::npos);
    EXPECT_NE(resp.find("\"error_code\":\"ROUTE_ANALYSIS_ERROR\""), std::string::npos);

    // Verify policy store has not changed
    EXPECT_EQ(store->load()->policy_identity, initial_identity);
    EXPECT_EQ(store->load()->generation, 1u);
}

TEST_F(TaperQueryAdminHttpServerTest, ApplyJsonEscapes) {
    // Valid policy containing escaped quotes, newline, tab, and double-backslashes (to test JSON
    // backslash escape parsing). Note: Inside the JSON string value, "\\" represents a single
    // backslash in JSON payload. In our JSON parser, "\\" is parsed to "\", resulting in "policy
    // \"escaped_\\_policy\"" in the TaperQuery source string. When this source string is fed to the
    // TaperQuery lexer/parser, the internal string is parsed properly.
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"policy \\\"escaped_\\\\\\\\_policy\\\" against sha \\\"" +
                       initial_identity +
                       "\\\" {\\n\\troute \\\"r1\\\" when path prefix \\\"/p\\\" {}\\n}\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"dry_run\"\n"
                       "}";

    std::string resp =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(resp.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(resp.find("\"status\":\"DryRunReady\""), std::string::npos);
}

TEST_F(TaperQueryAdminHttpServerTest, AuditLatestEndpoint) {
    // Initially no audit records
    std::string resp1 =
        send_http_request(handle.bound_port, "GET", "/admin/taperquery/audit/latest");
    EXPECT_NE(resp1.find("HTTP/1.1 404 Not Found"), std::string::npos);
    EXPECT_NE(resp1.find("NO_AUDIT_RECORD"), std::string::npos);

    // Perform an apply
    std::string policy_src = "policy \"my-policy\" against sha \"" + initial_identity +
                             "\" { route \"r1\" when path prefix \"/p\" {} }";
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"" +
                       escape_json(policy_src) +
                       "\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"apply\"\n"
                       "}";

    std::string resp_apply =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp_apply.find("HTTP/1.1 200 OK"), std::string::npos);

    // Now GET /audit/latest should return wrapped record
    std::string resp2 =
        send_http_request(handle.bound_port, "GET", "/admin/taperquery/audit/latest");
    EXPECT_NE(resp2.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(resp2.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(resp2.find("\"record\":{"), std::string::npos);
    EXPECT_NE(resp2.find("\"sequence\":1"), std::string::npos);
    EXPECT_NE(resp2.find("\"outcome\":\"Applied\""), std::string::npos);
    EXPECT_NE(resp2.find("\"route_changes\":["), std::string::npos);
    EXPECT_NE(resp2.find("\"issues\":["), std::string::npos);
}

TEST_F(TaperQueryAdminHttpServerTest, CurrentPolicyIncludesCompactAudit) {
    // Perform an apply
    std::string policy_src = "policy \"my-policy\" against sha \"" + initial_identity +
                             "\" { route \"r1\" when path prefix \"/p\" {} }";
    std::string body = "{\n"
                       "  \"source_type\": \"taperquery\",\n"
                       "  \"source\": \"" +
                       escape_json(policy_src) +
                       "\",\n"
                       "  \"expected_base_identity\": \"" +
                       initial_identity +
                       "\",\n"
                       "  \"mode\": \"apply\"\n"
                       "}";

    std::string resp_apply =
        send_http_request(handle.bound_port, "POST", "/admin/taperquery/apply", body);
    EXPECT_NE(resp_apply.find("HTTP/1.1 200 OK"), std::string::npos);

    // GET /policy/current
    std::string resp =
        send_http_request(handle.bound_port, "GET", "/admin/taperquery/policy/current");
    EXPECT_NE(resp.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(resp.find("\"has_latest_apply\":true"), std::string::npos);
    EXPECT_NE(resp.find("\"latest_apply\":{"), std::string::npos);
    EXPECT_EQ(resp.find("\"route_changes\":"), std::string::npos);
    EXPECT_EQ(resp.find("\"issues\":"), std::string::npos);
}

} // namespace bytetaper::admin::testing

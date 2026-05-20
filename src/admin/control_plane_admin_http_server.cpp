// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "admin/control_plane_admin_http_server.h"

#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_state_key.h"
#include "control_plane/runtime_status_report.h"
#include "runtime_policy/runtime_status_report.h"

#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace bytetaper::admin {

namespace {

using namespace bytetaper::control_plane;

std::string escape_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

struct ParsedApplyJson {
    bool ok = false;
    std::string error;
    std::string source;
    std::string request_id;
    std::string operator_id;
    std::uint64_t expected_base_generation = 0;
    std::string expected_base_policy_id;
};

static bool parse_json_string_field(const std::string& body, const std::string& key,
                                    std::string* out) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t colon = body.find(':', pos + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    size_t quote = body.find('"', colon + 1);
    if (quote == std::string::npos) {
        return false;
    }

    std::string value;
    for (size_t i = quote + 1; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') {
            *out = std::move(value);
            return true;
        }
        if (c == '\\' && i + 1 < body.size()) {
            const char next = body[i + 1];
            switch (next) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case '/':
                value.push_back('/');
                break;
            default:
                value.push_back(next);
                break;
            }
            i += 1;
            continue;
        }
        value.push_back(c);
    }
    return false;
}

static ParsedApplyJson parse_apply_json(const std::string& body) {
    ParsedApplyJson parsed{};
    parsed.ok = true;
    if (!parse_json_string_field(body, "source", &parsed.source) || parsed.source.empty()) {
        parsed.ok = false;
        parsed.error = "missing required field 'source'";
        return parsed;
    }
    parse_json_string_field(body, "request_id", &parsed.request_id);
    parse_json_string_field(body, "operator_id", &parsed.operator_id);
    parse_json_string_field(body, "expected_base_policy_id", &parsed.expected_base_policy_id);
    const std::string gen_key = "\"expected_base_generation\"";
    const size_t gen_pos = body.find(gen_key);
    if (gen_pos != std::string::npos) {
        size_t colon = body.find(':', gen_pos + gen_key.size());
        if (colon != std::string::npos) {
            size_t start = colon + 1;
            while (start < body.size() && (body[start] == ' ' || body[start] == '\t')) {
                ++start;
            }
            size_t end = start;
            while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end]))) {
                ++end;
            }
            if (end > start) {
                parsed.expected_base_generation =
                    std::strtoull(body.substr(start, end - start).c_str(), nullptr, 10);
            }
        }
    }
    return parsed;
}

static std::string parse_query_param(const std::string& path, const std::string& key) {
    const size_t q = path.find('?');
    if (q == std::string::npos) {
        return {};
    }
    const std::string query = path.substr(q + 1);
    const std::string needle = key + "=";
    const size_t pos = query.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    size_t end = query.find('&', start);
    if (end == std::string::npos) {
        end = query.size();
    }
    return query.substr(start, end - start);
}

static std::string strip_query(const std::string& path) {
    const size_t q = path.find('?');
    if (q == std::string::npos) {
        return path;
    }
    return path.substr(0, q);
}

static PolicyResourceKey parse_resource_key_query(const std::string& path) {
    const std::string value = parse_query_param(path, "resource_key");
    if (value.empty()) {
        return PolicyResourceKey::default_runtime();
    }
    PolicyResourceKey key{};
    if (parse_resource_key(value, &key)) {
        return key;
    }
    return PolicyResourceKey::default_runtime();
}

static std::string active_policy_json(const ActivePolicyResult& result) {
    if (!result.ok) {
        return std::string("{\"ok\":false,\"error\":\"") + escape_json_string(result.error) + "\"}";
    }
    std::string out = "{\"ok\":true";
    out += ",\"resource_key\":\"" + escape_json_string(result.resource_key) + "\"";
    out += ",\"generation\":" + std::to_string(result.active.generation);
    out += ",\"policy_id\":\"" + escape_json_string(result.active.policy_id) + "\"";
    out += ",\"canonical_hash\":\"" + escape_json_string(result.active.canonical_hash) + "\"";
    out += ",\"schema_version\":" + std::to_string(result.active.schema_version);
    out += ",\"api_version\":\"" + escape_json_string(result.active.api_version) + "\"";
    out += ",\"kind\":\"" + escape_json_string(result.active.kind) + "\"";
    out += ",\"source_type\":\"" + escape_json_string(result.active.source_type) + "\"";
    out += ",\"previous_generation\":" + std::to_string(result.active.previous_generation);
    out += "}";
    return out;
}

static std::string policy_version_json(const PolicyVersionFetchResult& result) {
    if (!result.ok) {
        return std::string("{\"ok\":false,\"error\":\"") + escape_json_string(result.error) + "\"}";
    }
    std::string out = "{\"ok\":true";
    out += ",\"generation\":" + std::to_string(result.record.generation);
    out += ",\"policy_id\":\"" + escape_json_string(result.record.policy_id) + "\"";
    out += ",\"canonical_hash\":\"" + escape_json_string(result.record.canonical_hash) + "\"";
    out += ",\"canonical_yaml\":\"";
    out += escape_json_string(result.canonical_yaml);
    out += "\"}";
    return out;
}

static std::string apply_result_json(const PolicyApplySubmitResult& result) {
    std::string out = "{\"ok\":";
    out += result.ok ? "true" : "false";
    out += ",\"status\":\"" + escape_json_string(to_string(result.status)) + "\"";
    out += ",\"resource_key\":\"" + escape_json_string(result.resource_key) + "\"";
    out += ",\"job_id\":\"" + escape_json_string(result.job_id) + "\"";
    out += ",\"message\":\"" + escape_json_string(result.message) + "\"";
    out += ",\"error\":\"" + escape_json_string(result.error) + "\"";
    out += "}";
    return out;
}

static std::string fleet_status_json(const FleetStatusResult& result) {
    if (!result.ok) {
        return std::string("{\"ok\":false,\"error\":\"") + escape_json_string(result.error) +
               "\",\"error_code\":\"" + escape_json_string(result.error_code) + "\"}";
    }
    const FleetPolicyStatus& s = result.status;
    std::string out = "{\"ok\":true";
    out += ",\"resource_key\":\"" + escape_json_string(s.resource_key) + "\"";
    out += ",\"committed\":{\"generation\":" + std::to_string(s.committed.generation);
    out += ",\"policy_id\":\"" + escape_json_string(s.committed.policy_id) + "\"";
    out += ",\"canonical_hash\":\"" + escape_json_string(s.committed.canonical_hash) + "\"}";
    out += ",\"fleet\":{\"runtime_count\":" + std::to_string(s.fleet.runtime_count);
    out += ",\"converged_count\":" + std::to_string(s.fleet.converged_count);
    out += ",\"stale_count\":" + std::to_string(s.fleet.stale_count);
    out += ",\"converged\":" + std::string(s.fleet.converged ? "true" : "false") + "}";
    out += ",\"runtimes\":[";
    for (std::size_t i = 0; i < s.runtimes.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        const RuntimeFleetEntry& r = s.runtimes[i];
        out += "{\"runtime_id\":\"" + escape_json_string(r.runtime_id) + "\"";
        out += ",\"active_generation\":" + std::to_string(r.active_generation);
        out += ",\"active_policy_id\":\"" + escape_json_string(r.active_policy_id) + "\"";
        out += ",\"activation_status\":\"" + escape_json_string(r.activation_status) + "\"";
        out += ",\"convergence_status\":\"" + escape_json_string(to_string(r.convergence_status)) +
               "\"";
        out += ",\"control_plane_reachable\":" +
               std::string(r.control_plane_reachable ? "true" : "false") + "}";
    }
    out += "]}";
    return out;
}

static bool parse_runtime_status_json(const std::string& body, RuntimeStatusReport* out,
                                      std::string* error) {
    if (out == nullptr) {
        return false;
    }
    parse_json_string_field(body, "runtimeId", &out->runtime_id);
    if (out->runtime_id.empty()) {
        parse_json_string_field(body, "runtime_id", &out->runtime_id);
    }
    parse_json_string_field(body, "resourceKey", &out->resource_key);
    if (out->resource_key.empty()) {
        parse_json_string_field(body, "resource_key", &out->resource_key);
    }
    parse_json_string_field(body, "activePolicyId", &out->active_policy_id);
    if (out->active_policy_id.empty()) {
        parse_json_string_field(body, "active_policy_id", &out->active_policy_id);
    }
    parse_json_string_field(body, "activeCanonicalHash", &out->active_canonical_hash);
    if (out->active_canonical_hash.empty()) {
        parse_json_string_field(body, "active_canonical_hash", &out->active_canonical_hash);
    }
    parse_json_string_field(body, "activationStatus", &out->activation_status);
    if (out->activation_status.empty()) {
        parse_json_string_field(body, "activation_status", &out->activation_status);
    }
    parse_json_string_field(body, "gatewayAdapter", &out->gateway_adapter);
    if (out->gateway_adapter.empty()) {
        parse_json_string_field(body, "gateway_adapter", &out->gateway_adapter);
    }
    parse_json_string_field(body, "dataPathMode", &out->data_path_mode);
    if (out->data_path_mode.empty()) {
        parse_json_string_field(body, "data_path_mode", &out->data_path_mode);
    }
    size_t gen_pos = body.find("\"activeGeneration\"");
    std::string gen_field = "\"activeGeneration\"";
    if (gen_pos == std::string::npos) {
        gen_pos = body.find("\"active_generation\"");
        gen_field = "\"active_generation\"";
    }
    if (gen_pos != std::string::npos) {
        const size_t colon = body.find(':', gen_pos + gen_field.size());
        if (colon != std::string::npos) {
            out->active_generation = std::strtoull(body.c_str() + colon + 1, nullptr, 10);
        }
    }
    if (body.find("\"controlPlaneReachable\":true") != std::string::npos ||
        body.find("\"control_plane_reachable\":true") != std::string::npos) {
        out->control_plane_reachable = true;
    }
    if (out->runtime_id.empty() || out->resource_key.empty()) {
        if (error != nullptr) {
            *error = "runtime status requires runtime_id and resource_key";
        }
        return false;
    }
    return validate_runtime_status_report(*out, error);
}

struct ControlPlaneAdminHttpServerImpl {
    ControlPlaneAdminHttpServerConfig config;
    int listen_fd = -1;
    std::atomic<bool> stop_flag{ false };
    std::thread accept_thread{};
};

static void send_response(int conn_fd, int status_code, const std::string& status_text,
                          const std::string& content_type, const std::string& body) {
    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    ssize_t total_sent = 0;
    while (total_sent < static_cast<ssize_t>(response.size())) {
        const ssize_t n = write(conn_fd, response.data() + static_cast<std::size_t>(total_sent),
                                response.size() - static_cast<std::size_t>(total_sent));
        if (n <= 0) {
            break;
        }
        total_sent += n;
    }
}

static void handle_connection(int conn_fd, ControlPlaneAdminHttpServerImpl* impl) {
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const auto& config = impl->config;
    char chunk[1024];
    std::string request_headers;
    size_t header_end_pos = std::string::npos;

    while (true) {
        const ssize_t n = read(conn_fd, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        request_headers.append(chunk, static_cast<std::size_t>(n));
        header_end_pos = request_headers.find("\r\n\r\n");
        if (header_end_pos != std::string::npos) {
            break;
        }
        if (request_headers.size() > config.max_request_bytes) {
            send_response(conn_fd, 413, "Payload Too Large", "application/json",
                          "{\"ok\":false,\"error\":\"payload too large\"}");
            close(conn_fd);
            return;
        }
    }

    if (header_end_pos == std::string::npos) {
        send_response(conn_fd, 400, "Bad Request", "application/json",
                      "{\"ok\":false,\"error\":\"incomplete headers\"}");
        close(conn_fd);
        return;
    }

    const size_t first_line_end = request_headers.find("\r\n");
    const std::string first_line = request_headers.substr(0, first_line_end);
    const size_t space1 = first_line.find(' ');
    const size_t space2 = first_line.find(' ', space1 + 1);
    const std::string method = first_line.substr(0, space1);
    const std::string path = first_line.substr(space1 + 1, space2 - (space1 + 1));
    const std::string route_path = strip_query(path);

    size_t content_length = 0;
    bool has_content_length = false;
    size_t pos = first_line_end + 2;
    while (pos < header_end_pos) {
        size_t next_line = request_headers.find("\r\n", pos);
        if (next_line == std::string::npos || next_line > header_end_pos) {
            next_line = header_end_pos;
        }
        std::string line = request_headers.substr(pos, next_line - pos);
        pos = next_line + 2;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = line.substr(0, colon);
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (name == "content-length") {
            content_length =
                static_cast<size_t>(std::strtoull(line.c_str() + colon + 1, nullptr, 10));
            has_content_length = true;
        }
    }

    std::string body;
    const size_t body_start = header_end_pos + 4;
    if (body_start < request_headers.size()) {
        body = request_headers.substr(body_start);
    }
    if (method == "POST" && has_content_length) {
        while (body.size() < content_length) {
            const ssize_t n = read(conn_fd, chunk, sizeof(chunk));
            if (n <= 0) {
                break;
            }
            body.append(chunk, static_cast<std::size_t>(n));
        }
    }

    if (route_path == "/healthz") {
        send_response(conn_fd, 200, "OK", "text/plain", "ok\n");
        close(conn_fd);
        return;
    }
    if (route_path == "/readyz") {
        const bool ready = config.control_plane_service != nullptr;
        send_response(conn_fd, ready ? 200 : 503, ready ? "OK" : "Service Unavailable",
                      "text/plain", ready ? "ok\n" : "not ready\n");
        close(conn_fd);
        return;
    }

    if (config.control_plane_service == nullptr) {
        send_response(conn_fd, 503, "Service Unavailable", "application/json",
                      "{\"ok\":false,\"error\":\"control plane service unavailable\"}");
        close(conn_fd);
        return;
    }

    if (route_path == "/admin/control-plane/policy/current" && method == "GET") {
        const PolicyResourceKey key = parse_resource_key_query(path);
        const ActivePolicyResult result = config.control_plane_service->get_active_policy(key);
        send_response(conn_fd, result.ok ? 200 : 404, result.ok ? "OK" : "Not Found",
                      "application/json", active_policy_json(result));
        close(conn_fd);
        return;
    }

    if (route_path == "/admin/control-plane/policy/version" && method == "GET") {
        const PolicyResourceKey key = parse_resource_key_query(path);
        const std::string generation_str = parse_query_param(path, "generation");
        if (generation_str.empty()) {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error\":\"generation query parameter required\"}");
            close(conn_fd);
            return;
        }
        const std::uint64_t generation = std::strtoull(generation_str.c_str(), nullptr, 10);
        const PolicyVersionFetchResult result =
            config.control_plane_service->get_policy_version(key, generation);
        send_response(conn_fd, result.ok ? 200 : 404, result.ok ? "OK" : "Not Found",
                      "application/json", policy_version_json(result));
        close(conn_fd);
        return;
    }

    if (route_path == "/admin/control-plane/policy/apply" && method == "POST") {
        const ParsedApplyJson parsed = parse_apply_json(body);
        if (!parsed.ok) {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error\":\"" + escape_json_string(parsed.error) + "\"}");
            close(conn_fd);
            return;
        }
        PolicyApplyRequest request{};
        request.resource_key = parse_resource_key_query(path);
        request.source_type = PolicyApplySourceType::Yaml;
        request.source = parsed.source;
        request.request_id = parsed.request_id;
        request.operator_id = parsed.operator_id;
        request.expected_base_generation = parsed.expected_base_generation;
        request.expected_base_policy_id = parsed.expected_base_policy_id;
        request.strict_production = config.strict_production_apply;
        if (!config.strict_production_apply) {
            request.include_unchanged_routes = true;
            request.include_field_level_changes = true;
        }
        const PolicyApplySubmitResult result = config.control_plane_service->apply(request);
        send_response(conn_fd, result.ok ? 200 : 400, result.ok ? "OK" : "Bad Request",
                      "application/json", apply_result_json(result));
        close(conn_fd);
        return;
    }

    if (route_path == "/admin/control-plane/fleet/status" && method == "GET") {
        const PolicyResourceKey key = parse_resource_key_query(path);
        const FleetStatusResult result = config.control_plane_service->get_fleet_status(key);
        send_response(conn_fd, result.ok ? 200 : 404, result.ok ? "OK" : "Not Found",
                      "application/json", fleet_status_json(result));
        close(conn_fd);
        return;
    }

    if (route_path == "/admin/control-plane/runtime/status" && method == "POST") {
        RuntimeStatusReport report{};
        std::string parse_error;
        if (!parse_runtime_status_json(body, &report, &parse_error)) {
            send_response(conn_fd, 400, "Bad Request", "application/json",
                          "{\"ok\":false,\"error\":\"" + escape_json_string(parse_error) + "\"}");
            close(conn_fd);
            return;
        }
        const RuntimeStatusReportResult result =
            config.control_plane_service->report_runtime_status(report);
        std::string res = "{\"ok\":";
        res += result.ok ? "true" : "false";
        res += ",\"error\":\"" + escape_json_string(result.error) + "\"";
        res += ",\"error_code\":\"" + escape_json_string(result.error_code) + "\"}";
        send_response(conn_fd, result.ok ? 200 : 400, result.ok ? "OK" : "Bad Request",
                      "application/json", res);
        close(conn_fd);
        return;
    }

    send_response(conn_fd, 404, "Not Found", "application/json",
                  "{\"ok\":false,\"error\":\"not found\"}");
    close(conn_fd);
}

static void accept_loop(ControlPlaneAdminHttpServerImpl* impl) {
    while (!impl->stop_flag.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int conn_fd =
            accept(impl->listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (conn_fd < 0) {
            if (impl->stop_flag.load()) {
                break;
            }
            continue;
        }
        handle_connection(conn_fd, impl);
    }
}

} // namespace

bool start_control_plane_admin_http_server(const ControlPlaneAdminHttpServerConfig& config,
                                           ControlPlaneAdminHttpServerHandle* handle) {
    if (handle == nullptr || config.control_plane_service == nullptr) {
        return false;
    }

    auto* impl = new ControlPlaneAdminHttpServerImpl();
    impl->config = config;

    impl->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (impl->listen_fd < 0) {
        delete impl;
        return false;
    }

    int opt = 1;
    setsockopt(impl->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    if (inet_pton(AF_INET, config.listen_address, &addr.sin_addr) != 1) {
        close(impl->listen_fd);
        delete impl;
        return false;
    }

    if (bind(impl->listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(impl->listen_fd);
        delete impl;
        return false;
    }

    if (listen(impl->listen_fd, 16) < 0) {
        close(impl->listen_fd);
        delete impl;
        return false;
    }

    socklen_t len = sizeof(addr);
    getsockname(impl->listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
    handle->bound_port = ntohs(addr.sin_port);
    handle->impl = impl;

    impl->accept_thread = std::thread(accept_loop, impl);
    return true;
}

void stop_control_plane_admin_http_server(ControlPlaneAdminHttpServerHandle* handle) {
    if (handle == nullptr || handle->impl == nullptr) {
        return;
    }
    auto* impl = static_cast<ControlPlaneAdminHttpServerImpl*>(handle->impl);
    impl->stop_flag.store(true);
    shutdown(impl->listen_fd, SHUT_RDWR);
    if (impl->accept_thread.joinable()) {
        impl->accept_thread.join();
    }
    close(impl->listen_fd);
    delete impl;
    handle->impl = nullptr;
    handle->bound_port = 0;
}

} // namespace bytetaper::admin

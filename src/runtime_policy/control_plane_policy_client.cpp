// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "runtime_policy/control_plane_policy_client.h"

#include "control_plane/control_plane_service.h"
#include "control_plane/runtime_status_report.h"
#include "runtime_policy/runtime_policy_pull_status.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace bytetaper::runtime_policy {

namespace {

std::int64_t now_unix_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

control_plane::RuntimeStatusReport
to_control_plane_report(const RuntimePolicyStatusReport& report) {
    control_plane::RuntimeStatusReport out{};
    out.runtime_id = report.runtime_id;
    out.resource_key = report.resource_key;
    out.gateway_adapter = report.gateway_adapter;
    out.active_generation = report.active_generation;
    out.active_policy_id = report.active_policy_id;
    out.active_canonical_hash = report.active_canonical_hash;
    out.activation_status = report.activation_status;
    out.control_plane_reachable = report.control_plane_reachable;
    out.data_path_mode = report.data_path_mode;
    out.last_pull_at_unix_epoch_ms = report.last_pull_at_unix_epoch_ms;
    out.last_activated_at_unix_epoch_ms = report.last_activated_at_unix_epoch_ms;
    out.last_error_code = report.last_error_code;
    out.last_error_message = report.last_error_message;
    out.received_at_unix_epoch_ms = now_unix_epoch_ms();
    return out;
}

std::string trim_trailing_slash(std::string endpoint) {
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }
    return endpoint;
}

struct HttpUrlParts {
    bool ok = false;
    std::string host;
    std::uint16_t port = 80;
    std::string path_prefix;
};

HttpUrlParts parse_http_endpoint(const std::string& endpoint) {
    HttpUrlParts parts{};
    std::string value = endpoint;
    if (value.rfind("http://", 0) == 0) {
        value = value.substr(7);
    }
    const size_t slash = value.find('/');
    const std::string hostport = slash == std::string::npos ? value : value.substr(0, slash);
    parts.path_prefix = slash == std::string::npos ? "" : value.substr(slash);
    if (parts.path_prefix.empty()) {
        parts.path_prefix = "/";
    }
    const size_t colon = hostport.find(':');
    if (colon == std::string::npos) {
        parts.host = hostport;
        parts.port = 80;
    } else {
        parts.host = hostport.substr(0, colon);
        parts.port =
            static_cast<std::uint16_t>(std::strtoul(hostport.c_str() + colon + 1, nullptr, 10));
    }
    parts.ok = !parts.host.empty();
    return parts;
}

struct HttpResponse {
    int status_code = 0;
    std::string body;
};

HttpResponse http_request(const std::string& endpoint, const char* method, const std::string& path,
                          const std::string& body, const char* content_type) {
    HttpResponse response{};
    const HttpUrlParts url = parse_http_endpoint(endpoint);
    if (!url.ok) {
        return response;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(url.host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return response;
    }

    int sock = -1;
    for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            continue;
        }
        sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(rp->ai_addr);
        addr->sin_port = htons(url.port);
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(result);
    if (sock < 0) {
        return response;
    }

    std::string request = std::string(method) + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + url.host + "\r\n";
    if (content_type != nullptr && body.size() > 0) {
        request += "Content-Type: " + std::string(content_type) + "\r\n";
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "Connection: close\r\n\r\n";
    request += body;

    ssize_t sent = 0;
    while (sent < static_cast<ssize_t>(request.size())) {
        const ssize_t n = write(sock, request.data() + static_cast<std::size_t>(sent),
                                request.size() - static_cast<std::size_t>(sent));
        if (n <= 0) {
            close(sock);
            return response;
        }
        sent += n;
    }

    char chunk[4096];
    std::string raw;
    while (true) {
        const ssize_t n = read(sock, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        raw.append(chunk, static_cast<std::size_t>(n));
    }
    close(sock);

    const size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return response;
    }
    const size_t status_pos = raw.find(' ');
    if (status_pos != std::string::npos) {
        response.status_code = std::atoi(raw.c_str() + static_cast<int>(status_pos + 1));
    }
    response.body = raw.substr(header_end + 4);
    return response;
}

bool json_field_bool(const std::string& body, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    return body.find("true", pos + needle.size()) != std::string::npos;
}

bool json_field_string(const std::string& body, const char* key, std::string* out) {
    if (out == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
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

std::uint64_t json_field_uint64(const std::string& body, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t pos = body.find(needle);
    if (pos == std::string::npos) {
        return 0;
    }
    return std::strtoull(body.c_str() + pos + needle.size(), nullptr, 10);
}

} // namespace

InProcessControlPlanePolicyClient::InProcessControlPlanePolicyClient(
    control_plane::ControlPlaneService* service)
    : service_(service) {}

ActivePolicyPointerResult InProcessControlPlanePolicyClient::get_active_policy(
    const control_plane::PolicyResourceKey& resource_key) {
    ActivePolicyPointerResult out{};
    if (service_ == nullptr) {
        out.error = "control plane service is not configured";
        out.error_code = kErrControlPlaneUnavailable;
        return out;
    }

    const control_plane::ActivePolicyResult res = service_->get_active_policy(resource_key);
    if (!res.ok) {
        out.error = res.error;
        out.error_code = kErrActivePolicyQueryFailed;
        return out;
    }

    out.ok = true;
    out.active = res.active;
    return out;
}

CanonicalPolicyResult InProcessControlPlanePolicyClient::get_policy_version(
    const control_plane::PolicyResourceKey& resource_key, std::uint64_t generation) {
    CanonicalPolicyResult out{};
    if (service_ == nullptr) {
        out.error = "control plane service is not configured";
        out.error_code = kErrControlPlaneUnavailable;
        return out;
    }

    const control_plane::PolicyVersionFetchResult res =
        service_->get_policy_version(resource_key, generation);
    if (!res.ok) {
        out.error = res.error;
        out.error_code = kErrPolicyVersionFetchFailed;
        return out;
    }

    out.ok = true;
    out.record = res.record;
    out.canonical_yaml = res.canonical_yaml;
    return out;
}

RuntimeStatusReportResult
InProcessControlPlanePolicyClient::report_runtime_status(const RuntimePolicyStatusReport& report) {
    last_report_ = report;
    RuntimeStatusReportResult out{};
    if (service_ == nullptr) {
        out.error = "control plane service is not configured";
        out.error_code = kErrControlPlaneUnavailable;
        return out;
    }

    const control_plane::RuntimeStatusReportResult cp_result =
        service_->report_runtime_status(to_control_plane_report(report));
    out.ok = cp_result.ok;
    out.error = cp_result.error;
    out.error_code = cp_result.error_code;
    return out;
}

const RuntimePolicyStatusReport& InProcessControlPlanePolicyClient::last_report() const {
    return last_report_;
}

HttpControlPlanePolicyClient::HttpControlPlanePolicyClient(std::string endpoint)
    : endpoint_(trim_trailing_slash(std::move(endpoint))) {}

ActivePolicyPointerResult
HttpControlPlanePolicyClient::get_active_policy(const control_plane::PolicyResourceKey& key) {
    ActivePolicyPointerResult out{};
    const std::string path = "/admin/control-plane/policy/current?resource_key=" + key.to_string();
    const HttpResponse http_res = http_request(endpoint_, "GET", path, "", nullptr);
    if (http_res.status_code != 200) {
        out.error = "control plane active policy request failed with status " +
                    std::to_string(http_res.status_code);
        out.error_code = kErrControlPlaneUnavailable;
        return out;
    }
    if (!json_field_bool(http_res.body, "ok")) {
        std::string err;
        json_field_string(http_res.body, "error", &err);
        out.error = err.empty() ? "control plane active policy response not ok" : err;
        out.error_code = kErrActivePolicyQueryFailed;
        return out;
    }

    out.ok = true;
    out.active.generation = json_field_uint64(http_res.body, "generation");
    out.active.previous_generation = json_field_uint64(http_res.body, "previous_generation");
    json_field_string(http_res.body, "policy_id", &out.active.policy_id);
    json_field_string(http_res.body, "canonical_hash", &out.active.canonical_hash);
    out.active.schema_version =
        static_cast<std::uint32_t>(json_field_uint64(http_res.body, "schema_version"));
    json_field_string(http_res.body, "api_version", &out.active.api_version);
    json_field_string(http_res.body, "kind", &out.active.kind);
    json_field_string(http_res.body, "source_type", &out.active.source_type);
    return out;
}

CanonicalPolicyResult
HttpControlPlanePolicyClient::get_policy_version(const control_plane::PolicyResourceKey& key,
                                                 std::uint64_t generation) {
    CanonicalPolicyResult out{};
    const std::string path = "/admin/control-plane/policy/version?resource_key=" + key.to_string() +
                             "&generation=" + std::to_string(generation);
    const HttpResponse http_res = http_request(endpoint_, "GET", path, "", nullptr);
    if (http_res.status_code != 200) {
        out.error = "control plane policy version request failed with status " +
                    std::to_string(http_res.status_code);
        out.error_code = kErrPolicyVersionFetchFailed;
        return out;
    }
    if (!json_field_bool(http_res.body, "ok")) {
        std::string err;
        json_field_string(http_res.body, "error", &err);
        out.error = err.empty() ? "control plane policy version response not ok" : err;
        out.error_code = kErrPolicyVersionFetchFailed;
        return out;
    }

    out.ok = true;
    out.record.generation = json_field_uint64(http_res.body, "generation");
    json_field_string(http_res.body, "policy_id", &out.record.policy_id);
    json_field_string(http_res.body, "canonical_hash", &out.record.canonical_hash);
    json_field_string(http_res.body, "canonical_yaml", &out.canonical_yaml);
    return out;
}

RuntimeStatusReportResult
HttpControlPlanePolicyClient::report_runtime_status(const RuntimePolicyStatusReport& report) {
    RuntimeStatusReportResult out{};
    const std::string body = serialize_runtime_status_report(report);
    const HttpResponse http_res = http_request(
        endpoint_, "POST", "/admin/control-plane/runtime/status", body, "application/json");
    if (http_res.status_code != 200) {
        out.error = "control plane runtime status request failed with status " +
                    std::to_string(http_res.status_code);
        out.error_code = kErrRuntimeStatusReportFailed;
        return out;
    }
    out.ok = json_field_bool(http_res.body, "ok");
    if (!out.ok) {
        json_field_string(http_res.body, "error", &out.error);
        json_field_string(http_res.body, "error_code", &out.error_code);
    }
    return out;
}

} // namespace bytetaper::runtime_policy

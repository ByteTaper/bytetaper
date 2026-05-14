// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TESTS_TAPERQUERY_TEST_HELPERS_H
#define BYTETAPER_TESTS_TAPERQUERY_TEST_HELPERS_H

#include "runtime/policy_snapshot.h"
#include "taperquery/tq_apply_service.h"

#include <arpa/inet.h>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace bytetaper::testing {

inline std::shared_ptr<const runtime::RuntimePolicySnapshot>
make_snapshot_with_routes(const std::vector<policy::RoutePolicy>& routes,
                          const std::string& source_name, std::uint64_t generation) {
    auto res = runtime::build_runtime_policy_snapshot_from_routes(routes.data(), routes.size(),
                                                                  source_name.c_str(), generation);
    return res.snapshot;
}

inline taperquery::TqApplyRequest
make_valid_apply_request(const std::string& source, const std::string& expected_base,
                         taperquery::TqApplyMode mode = taperquery::TqApplyMode::Apply) {
    taperquery::TqApplyRequest req;
    req.request_id = "test-req-id";
    req.operator_id = "test-operator";
    req.mode = mode;
    req.expected_base_identity = expected_base;
    req.source = source;
    return req;
}

inline std::string make_taperquery_source(const std::string& route_id, const std::string& prefix,
                                          const std::string& sha = "base-sha") {
    return "policy \"test-policy\" against sha \"" + sha +
           "\" {\n"
           "  route \"" +
           route_id + "\" when path prefix \"" + prefix +
           "\" {}\n"
           "}";
}

struct HttpTestResponse {
    int status_code = 0;
    std::string status_message;
    std::string body;
    std::string raw_response;
};

inline HttpTestResponse admin_http_request(std::uint16_t port, const std::string& method,
                                           const std::string& path, const std::string& body = "") {
    HttpTestResponse res;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return res;

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return res;
    }

    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    if (!body.empty() || method == "POST") {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        request += "Content-Type: application/json\r\n";
    }
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

    res.raw_response = response;

    auto pos = response.find("HTTP/1.1 ");
    if (pos != std::string::npos) {
        pos += 9;
        auto space = response.find(' ', pos);
        if (space != std::string::npos) {
            res.status_code = std::stoi(response.substr(pos, space - pos));
            auto eol = response.find("\r\n", space);
            if (eol != std::string::npos) {
                res.status_message = response.substr(space + 1, eol - space - 1);
            }
        }
    }

    auto body_pos = response.find("\r\n\r\n");
    if (body_pos != std::string::npos) {
        res.body = response.substr(body_pos + 4);
    }

    return res;
}

} // namespace bytetaper::testing

#endif // BYTETAPER_TESTS_TAPERQUERY_TEST_HELPERS_H

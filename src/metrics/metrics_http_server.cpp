// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/metrics_http_server.h"

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace bytetaper::metrics {

namespace {

struct MetricsHttpServerImpl {
    MetricsRegistry* registry = nullptr;
    RuntimeHealthState* health_state = nullptr;
    int listen_fd = -1;
    std::atomic<bool> stop_flag{ false };
    std::thread accept_thread{};
};

void handle_connection(int conn_fd, MetricsHttpServerImpl* impl) {
    char buffer[4096];
    std::string request;
    while (true) {
        ssize_t n = read(conn_fd, buffer, sizeof(buffer));
        if (n <= 0)
            break;
        request.append(buffer, n);
        if (request.find("\r\n\r\n") != std::string::npos)
            break;
        if (request.size() > 4096)
            break;
    }

    std::string response;
    if (request.find("GET /metrics") != std::string::npos) {
        std::string body = render_prometheus_text(*(impl->registry));
        response = "HTTP/1.0 200 OK\r\n";
        response += "Content-Type: text/plain; version=0.0.4\r\n";
        response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        response += "\r\n";
        response += body;
    } else if (request.find("GET /healthz") != std::string::npos) {
        std::string body = "ok\n";
        response = "HTTP/1.0 200 OK\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        response += "\r\n";
        response += body;
    } else if (request.find("GET /readyz") != std::string::npos) {
        if (impl->health_state == nullptr) {
            std::string body = "health state unavailable\n";
            response = "HTTP/1.0 503 Service Unavailable\r\n";
            response += "Content-Type: text/plain\r\n";
            response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            response += "\r\n";
            response += body;
        } else if (impl->health_state->ready.load(std::memory_order_acquire)) {
            std::string body = "ok\n";
            response = "HTTP/1.0 200 OK\r\n";
            response += "Content-Type: text/plain\r\n";
            response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            response += "\r\n";
            response += body;
        } else {
            const char* reason =
                impl->health_state->not_ready_reason.load(std::memory_order_acquire);
            std::string body = std::string(reason ? reason : "unknown reason") + "\n";
            response = "HTTP/1.0 503 Service Unavailable\r\n";
            response += "Content-Type: text/plain\r\n";
            response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            response += "\r\n";
            response += body;
        }
    } else {
        response = "HTTP/1.0 404 Not Found\r\n\r\n";
    }

    ssize_t total_sent = 0;
    while (total_sent < (ssize_t) response.size()) {
        ssize_t n = write(conn_fd, response.data() + total_sent, response.size() - total_sent);
        if (n <= 0)
            break;
        total_sent += n;
    }
    close(conn_fd);
}

void accept_loop(MetricsHttpServerImpl* impl) {
    while (!impl->stop_flag.load(std::memory_order_relaxed)) {
        int conn_fd = accept(impl->listen_fd, nullptr, nullptr);
        if (conn_fd < 0) {
            if (impl->stop_flag.load(std::memory_order_relaxed))
                break;
            continue;
        }
        handle_connection(conn_fd, impl);
    }
}

} // namespace

bool start_metrics_http_server(const MetricsHttpServerConfig& config,
                               MetricsHttpServerHandle* handle) {
    if (handle == nullptr || config.registry == nullptr) {
        return false;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    inet_pton(AF_INET, config.listen_address, &addr.sin_addr);

    if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        return false;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr*) &addr, &len) == 0) {
        handle->bound_port = ntohs(addr.sin_port);
    }

    auto* impl = new MetricsHttpServerImpl();
    impl->registry = config.registry;
    impl->health_state = config.health_state;
    impl->listen_fd = fd;
    impl->accept_thread = std::thread(accept_loop, impl);

    handle->impl = impl;
    return true;
}

void stop_metrics_http_server(MetricsHttpServerHandle* handle) {
    if (handle == nullptr || handle->impl == nullptr) {
        return;
    }

    auto* impl = static_cast<MetricsHttpServerImpl*>(handle->impl);
    impl->stop_flag.store(true, std::memory_order_relaxed);

    // Shutdown unblocks accept()
    shutdown(impl->listen_fd, SHUT_RDWR);
    close(impl->listen_fd);

    if (impl->accept_thread.joinable()) {
        impl->accept_thread.join();
    }

    delete impl;
    handle->impl = nullptr;
    handle->bound_port = 0;
}

} // namespace bytetaper::metrics

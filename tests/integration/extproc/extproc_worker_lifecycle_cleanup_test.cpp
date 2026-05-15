// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/grpc_server.h"
#include "metrics/prometheus_registry.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace bytetaper::extproc {
namespace {

struct ReservedPort {
    int fd = -1;
    std::uint16_t port = 0;

    ReservedPort() = default;
    ReservedPort(const ReservedPort&) = delete;
    ReservedPort& operator=(const ReservedPort&) = delete;

    ReservedPort(ReservedPort&& other) noexcept : fd(other.fd), port(other.port) {
        other.fd = -1;
        other.port = 0;
    }

    ReservedPort& operator=(ReservedPort&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (fd >= 0) {
            close(fd);
        }
        fd = other.fd;
        port = other.port;
        other.fd = -1;
        other.port = 0;
        return *this;
    }

    ~ReservedPort() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

ReservedPort reserve_loopback_port() {
    ReservedPort reserved{};
    reserved.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (reserved.fd < 0) {
        return reserved;
    }

    int reuse = 1;
    (void) setsockopt(reserved.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(reserved.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(reserved.fd);
        reserved.fd = -1;
        return reserved;
    }

    if (listen(reserved.fd, 1) != 0) {
        close(reserved.fd);
        reserved.fd = -1;
        return reserved;
    }

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (getsockname(reserved.fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0) {
        close(reserved.fd);
        reserved.fd = -1;
        return reserved;
    }

    reserved.port = ntohs(bound.sin_port);
    return reserved;
}

std::string listen_address_for_reserved_port(std::uint16_t port) {
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "127.0.0.1:%u", static_cast<unsigned>(port));
    return std::string(buf);
}

} // namespace

TEST(WorkerLifecycleCleanupTest, ShutsDownWorkersOnServerStartFailure) {
    ReservedPort reserved = reserve_loopback_port();
    ASSERT_GE(reserved.fd, 0);
    ASSERT_NE(reserved.port, 0);

    const std::string listen_address = listen_address_for_reserved_port(reserved.port);

    GrpcServerConfig config{};
    config.listen_address = listen_address.c_str();

    metrics::MetricsRegistry metrics_reg{};
    config.metrics_registry = &metrics_reg;

    GrpcServerHandle handle{};

    const bool started = start_grpc_server(config, &handle);
    EXPECT_FALSE(started);

    if (started) {
        stop_grpc_server(&handle);
        GTEST_FAIL() << "Server started unexpectedly on an already reserved port";
    }

    EXPECT_EQ(handle.impl, nullptr);
    EXPECT_EQ(handle.bound_port, 0);
}

TEST(WorkerLifecycleCleanupTest, RepeatedStartFailureNoLeak) {
    for (int i = 0; i < 50; ++i) {
        ReservedPort reserved = reserve_loopback_port();
        ASSERT_GE(reserved.fd, 0);
        ASSERT_NE(reserved.port, 0);

        const std::string listen_address = listen_address_for_reserved_port(reserved.port);

        GrpcServerConfig config{};
        config.listen_address = listen_address.c_str();
        metrics::MetricsRegistry metrics_reg{};
        config.metrics_registry = &metrics_reg;

        GrpcServerHandle handle{};
        const bool started = start_grpc_server(config, &handle);
        EXPECT_FALSE(started);

        if (started) {
            stop_grpc_server(&handle);
            GTEST_FAIL() << "Server started unexpectedly on an already reserved port";
        }

        EXPECT_EQ(handle.impl, nullptr);
        EXPECT_EQ(handle.bound_port, 0);
    }

    // If worker resources were leaking on failed starts, repeated attempts would
    // eventually hang or exhaust available runtime resources.
}

} // namespace bytetaper::extproc

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

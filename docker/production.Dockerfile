# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

# Stage 1: Compile release binary inside the builder environment
FROM bytetaper-dev:latest AS builder

WORKDIR /workspace
COPY . .

RUN cmake -S . -B /tmp/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBYTETAPER_ENABLE_INTEGRATION_TESTS=ON \
      -DBYTETAPER_ENABLE_GTEST_TESTS=OFF \
 && cmake --build /tmp/build --target bytetaper-extproc-server

# Stage 2: Minimal runtime image
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgflags2.2 \
    liblz4-1 \
    libsnappy1v5 \
    libzstd1 \
    libbz2-1.0 \
    zlib1g \
    libgrpc++1.51t64 \
    libprotobuf32t64 \
    libyaml-cpp0.8 \
 && rm -rf /var/lib/apt/lists/*

# RocksDB built from source — copy the main library file and recreate symlinks
COPY --from=builder /usr/local/lib/librocksdb.so.11.1.1 /usr/local/lib/
RUN ln -s librocksdb.so.11.1.1 /usr/local/lib/librocksdb.so.11.1 \
 && ln -s librocksdb.so.11.1.1 /usr/local/lib/librocksdb.so.11 \
 && ln -s librocksdb.so.11.1.1 /usr/local/lib/librocksdb.so \
 && ldconfig

# Server binary
COPY --from=builder /tmp/build/bytetaper-extproc-server /usr/local/bin/bytetaper-extproc-server

# License files (required by Apache-2.0 and AGPL-3.0)
COPY LICENSES/ /opt/bytetaper/LICENSES/
COPY THIRD_PARTY_NOTICES.md /opt/bytetaper/

RUN groupadd -r bytetaper && useradd -r -g bytetaper -u 1001 bytetaper

USER bytetaper
EXPOSE 18080 18081

ENTRYPOINT ["bytetaper-extproc-server"]
CMD ["--help"]

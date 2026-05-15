# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

ARG ROCKSPACK_IMAGE=haluan/rockspack:11.1.1-ubuntu26.04-6cdeb9d

ARG BYTETAPER_VERSION=dev
ARG BYTETAPER_GIT_SHA=unknown
ARG BYTETAPER_BUILD_DATE=unknown

# --- Stage 1: ByteTaper Builder Environment ---
FROM ${ROCKSPACK_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive

ARG BYTETAPER_VERSION
ARG BYTETAPER_GIT_SHA
ARG BYTETAPER_BUILD_DATE

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    ninja-build \
    pkg-config \
    python3 \
    libbz2-dev \
    libgflags-dev \
    libgrpc++-dev \
    liblz4-dev \
    libprotobuf-dev \
    libsnappy-dev \
    libyaml-cpp-dev \
    libzstd-dev \
    zlib1g-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*

RUN ldconfig \
    && test -f /usr/local/include/rocksdb/db.h \
    && ldconfig -p | grep -q librocksdb

WORKDIR /workspace
COPY . .

RUN cmake -S . -B /tmp/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBYTETAPER_ENABLE_INTEGRATION_TESTS=ON \
      -DBYTETAPER_ENABLE_GTEST_TESTS=OFF \
      -DBYTETAPER_VERSION="${BYTETAPER_VERSION}" \
      -DBYTETAPER_GIT_SHA="${BYTETAPER_GIT_SHA}" \
      -DBYTETAPER_BUILD_DATE="${BYTETAPER_BUILD_DATE}" \
 && cmake --build /tmp/build --target bytetaper-extproc-server

# --- Stage 2: Minimal Runtime Image ---
FROM ubuntu:26.04 AS runtime

ARG BYTETAPER_VERSION=dev
ARG BYTETAPER_GIT_SHA=unknown
ARG BYTETAPER_BUILD_DATE=unknown

LABEL org.opencontainers.image.title="ByteTaper Runtime"
LABEL org.opencontainers.image.description="ByteTaper API Performance Gateway runtime image"
LABEL org.opencontainers.image.source="https://github.com/haluan/bytetaper"
LABEL org.opencontainers.image.revision="${BYTETAPER_GIT_SHA}"
LABEL org.opencontainers.image.version="${BYTETAPER_VERSION}"
LABEL org.opencontainers.image.created="${BYTETAPER_BUILD_DATE}"
LABEL org.opencontainers.image.licenses="AGPL-3.0-only OR LicenseRef-Commercial"

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

COPY --from=builder /usr/local/lib/librocksdb.so* /usr/local/lib/
RUN ldconfig && ldconfig -p | grep -q librocksdb

COPY --from=builder /tmp/build/bytetaper-extproc-server /usr/local/bin/bytetaper-extproc-server

COPY LICENSES/ /opt/bytetaper/LICENSES/
COPY THIRD_PARTY_NOTICES.md /opt/bytetaper/

RUN echo "{\"name\":\"bytetaper\",\"component\":\"bytetaper-extproc-server\",\"version\":\"${BYTETAPER_VERSION}\",\"git_sha\":\"${BYTETAPER_GIT_SHA}\",\"build_date\":\"${BYTETAPER_BUILD_DATE}\",\"build_type\":\"Release\",\"license\":\"AGPL-3.0-only OR LicenseRef-Commercial\"}" \
    > /opt/bytetaper/build-info.json

RUN groupadd -r bytetaper && useradd -r -g bytetaper -u 1001 bytetaper

RUN mkdir -p /etc/bytetaper \
             /var/lib/bytetaper/l2-cache \
             /var/run/bytetaper \
 && chown -R bytetaper:bytetaper /var/lib/bytetaper /var/run/bytetaper

USER bytetaper
EXPOSE 18080 18081

ENTRYPOINT ["bytetaper-extproc-server"]
CMD ["--help"]
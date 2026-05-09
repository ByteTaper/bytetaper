# --- Stage 1: RocksDB Builder ---
FROM ubuntu:24.04 AS rocksdb-builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    git \
    libbz2-dev \
    libgflags-dev \
    liblz4-dev \
    libsnappy-dev \
    libzstd-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/rocksdb-src
RUN git clone --depth 1 --branch v11.1.1 https://github.com/facebook/rocksdb.git .

# Build shared library with optimization and portability enabled
RUN DEBUG_LEVEL=0 PORTABLE=1 make -j$(nproc) shared_lib

# Install to standard path in the builder stage
RUN make install-shared INSTALL_PATH=/usr/local

# --- Stage 2: Development Environment ---
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

ARG LOCAL_UID=1000
ARG LOCAL_GID=1000

# Install ByteTaper development dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    ca-certificates \
    ccache \
    clang-format \
    cmake \
    git \
    curl \
    make \
    ninja-build \
    pkg-config \
    python3 \
    # Build-time dependencies for ByteTaper & RocksDB runtime
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

# Selectively copy only the necessary RocksDB artifacts
COPY --from=rocksdb-builder /usr/local/lib/librocksdb.so* /usr/local/lib/
COPY --from=rocksdb-builder /usr/local/include/rocksdb /usr/local/include/rocksdb

# Ensure the dynamic linker can find RocksDB
RUN ldconfig

ENV CCACHE_DIR=/home/bytetaper/.cache/ccache
ENV CCACHE_MAXSIZE=5G
ENV CCACHE_COMPRESS=true

# Setup development user and cache directory
RUN groupadd -f -o -g "${LOCAL_GID}" bytetaper \
    && useradd -m -o -u "${LOCAL_UID}" -g "${LOCAL_GID}" -s /bin/bash bytetaper \
    && mkdir -p /home/bytetaper/.cache/ccache \
    && mkdir -p /var/cache/bytetaper \
    && chown -R bytetaper:bytetaper /home/bytetaper/.cache \
    && chown bytetaper:bytetaper /var/cache/bytetaper

WORKDIR /workspace
USER bytetaper

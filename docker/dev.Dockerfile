ARG ROCKSPACK_IMAGE=haluan/rockspack:11.1.1-ubuntu26.04-6cdeb9d

FROM ${ROCKSPACK_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

ARG LOCAL_UID=1000
ARG LOCAL_GID=1000

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

ENV CCACHE_DIR=/home/bytetaper/.cache/ccache
ENV CCACHE_MAXSIZE=5G
ENV CCACHE_COMPRESS=true

RUN groupadd -f -o -g "${LOCAL_GID}" bytetaper \
    && useradd -m -o -u "${LOCAL_UID}" -g "${LOCAL_GID}" -s /bin/bash bytetaper \
    && mkdir -p /home/bytetaper/.cache/ccache \
    && mkdir -p /var/cache/bytetaper \
    && chown -R bytetaper:bytetaper /home/bytetaper/.cache \
    && chown bytetaper:bytetaper /var/cache/bytetaper

WORKDIR /workspace
USER bytetaper
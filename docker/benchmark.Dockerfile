# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

FROM alpine:3.19

# Install wrk and other runtime dependencies
# Note: wrk2 is unavailable on ARM64 due to upstream LuaJIT compatibility issues.
# We use wrk (v4.2.0+) which is natively supported and stable.
RUN apk add --no-cache \
    bash \
    curl \
    jq \
    wrk \
    python3

WORKDIR /workspace

# Default entrypoint
ENTRYPOINT ["/bin/bash"]
CMD ["./benchmarks/run_benchmark.sh"]

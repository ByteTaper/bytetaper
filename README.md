# ByteTaper: API response optimization without backend rewrites

![CI](https://github.com/haluan/bytetaper/actions/workflows/ci.yml/badge.svg)

ByteTaper is an API Performance Gateway component designed to optimize API
responses at the edge without requiring backend rewrites. The current runtime is
implemented as an Envoy External Processor (`ext_proc`) service.

Core primitives include field selection, policy safety, tiered caching,
pagination guardrails, compression decisions, request coalescing, materialized
field-filtered variant caching, and Prometheus-style observability.

## Requirements

ByteTaper is developed and tested through Docker Compose only. Do not install or
run CMake, compilers, RocksDB, gRPC, Protobuf, yaml-cpp, or Envoy directly on
the host for normal development.

Required host tools:

- Docker Engine or Docker Desktop with Linux container support
- Docker Compose v2 (`docker compose`)
- Git
- A shell capable of running standard Docker commands

Optional host tools:

- GNU Make, only for repository wrapper commands such as `make format`
- `docker-compose`, only if your environment does not provide `docker compose`

Resource notes:

- The first build downloads and builds development dependencies, including
  RocksDB, so it can take longer than later incremental builds.
- Docker volumes are used for the build directory and ccache.
- Containers support host-mapped UID/GID to avoid root-owned files in mounted
  workspaces.

## Docker Development Workflow

Build the reusable development image first:

```bash
docker compose build bytetaper-dev
```

Run a development build:

```bash
docker compose run --rm bytetaper-dev-build
```

Build the ExtProc server target used by the local stack:

```bash
docker compose run --rm bytetaper-build-server
```

Run unit tests:

```bash
docker compose run --rm bytetaper-unit-test
```

Run smoke tests:

```bash
docker compose run --rm bytetaper-smoke-test
```

Run integration tests:

```bash
docker compose run --rm bytetaper-integration-test
```

Run sanitizer verification tests:

- **ThreadSanitizer (TSAN)** covers dynamic data race validation across all concurrency test targets:
  ```bash
  docker compose run --rm bytetaper-tsan-test
  ```
- **AddressSanitizer (ASAN)** tracks memory violations, leaks, and bounds checks:
  ```bash
  docker compose run --rm bytetaper-asan-test
  ```
- **UndefinedBehaviorSanitizer (UBSAN)** validates clean numerical behaviors and null references:
  ```bash
  docker compose run --rm bytetaper-ubsan-test
  ```

Open a development shell:

```bash
docker compose run --rm bytetaper-dev-shell
```

Format code:

```bash
make format
```

Compatibility note: if `docker compose` is unavailable, use the equivalent
`docker-compose` command form.

Rootless note:

```bash
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose run --rm bytetaper-unit-test
```

## Docker Images

ByteTaper utilizes two primary Docker images tailored for distinct roles:

### 1. Reusable Development Image (`bytetaper-dev:latest`)
- **Purpose**: Compiling, building, formatting, and running tests.
- **Contents**: Full compiler and build toolchain (C++ compiler, CMake, Ninja, ccache), dynamic library headers (gRPC, Protobuf, Yaml-CPP, RocksDB), and testing frameworks.
- **Local build command**:
  ```bash
  docker compose build bytetaper-dev
  ```

### 2. Production Runtime Image (`bytetaper-runtime:latest`)
- **Purpose**: Minimal, secure, high-performance production runner (~470 MB).
- **Contents**: Release-compiled `bytetaper-extproc-server` binary, stripped shared libraries, policies, and license/attribution files. It excludes compilers or development dependencies and runs under a secure, non-root `bytetaper` user (`uid=1001`).
- **Build and validation workflow**:
  ```bash
  # 1. Compile the release target binary inside the dev image:
  LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose run --rm bytetaper-build-server

  # 2. Build the optimized production runtime image:
  docker compose --profile runtime-test build bytetaper-runtime-image-build

  # 3. Run the automated production smoke validation suite:
  LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose --profile runtime-test run --rm bytetaper-runtime-smoke-validator
  ```

## Run ByteTaper with Docker Compose

The default local stack uses:

- `mock-api`: local Python mock upstream API
- `bytetaper-extproc`: ByteTaper Envoy External Processor service
- `envoy`: Envoy configured to call ByteTaper through `ext_proc`

Build the development image first if needed:

```bash
docker compose build bytetaper-dev
```

Start the local stack:

```bash
docker compose up bytetaper-extproc envoy
```

The `bytetaper-extproc` service exposes:

- `18080`: ExtProc server listen port
- `18081`: HTTP server exposing `/metrics` (Prometheus), `/healthz` (liveness), and `/readyz` (readiness) endpoints

Stop the local stack:

```bash
docker compose down
```

## Policy Validation

Validate a policy file through Docker:

```bash
docker compose run --rm bytetaper-build-server build/bytetaper-validate-policy examples/policy/bytetaper-policy.yaml
```

Exit codes:

- `0`: success
- `1`: usage error
- `2`: YAML parse/load failure
- `3`: validation rule violation

## Documentation

- [Route Policy Reference](docs/route-policy.md)
- [Field Selection Reference](docs/field-selection.md)
- [Observability Guide](docs/observability.md)
- [Policy Safety](docs/policy-safety.md)
- [Cache Policy](docs/cache-policy.md)
- [Pagination Policy](docs/pagination-policy.md)
- [Compression Policy](docs/compression-policy.md)
- [Request Coalescing](docs/coalescing-policy.md)
- [Materialized Field-Filtered Variant Cache](docs/materialized-filed-filtered-variant-cache.policy)
- [Runtime Execution Boundaries](docs/runtime/RUNTIME_BOUNDARIES.md)
- [Compiled Route Runtime](docs/runtime/COMPILED_ROUTE_RUNTIME.md)
- [Body Size Contract](docs/runtime/BODY_SIZE_CONTRACT.md)
- [Runtime Configuration Reference](docs/runtime/CONFIGURATION.md)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for details on the development process
and contribution expectations.

## License

This tool is licensed under:

- `AGPL-3.0-only`, or
- `LicenseRef-Commercial`

See repository license files and source SPDX headers for details. See
`LICENSES/` for full license texts.

# ByteTaper: API response optimization without backend rewrites

![CI](https://github.com/haluan/bytetaper/actions/workflows/ci.yml/badge.svg)

## Development Commands

Use Docker Compose workflows only. Do not run local host build/test commands.

Build/demo flow:

```bash
docker compose run --rm bytetaper-build
```

Unit-test flow:

```bash
docker compose run --rm bytetaper-unit-test
make test
```

Smoke-test flow:

```bash
docker compose run --rm bytetaper-smoke-test
make smoke-test
```

Integration-test flow:

```bash
docker compose run --rm bytetaper-integration-test
```

Format flow:

```bash
make format
```

Compatibility note:
If `docker compose` is unavailable in your environment, use the equivalent
`docker-compose` command form.

Rootless note:
Containers run with host-mapped UID/GID to avoid root-owned files in mounted
workspaces. Example:

```bash
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) docker compose run --rm bytetaper-unit-test
```

## Envoy ExtProc Adapter

The extproc adapter is a high-performance gRPC implementation of the Envoy `ExternalProcessor` service.

### Features

- **gRPC Server Lifecycle**: Managed through `GrpcServerHandle` with support for explicit startup, dynamic port binding, and clean shutdown.
- **ExternalProcessor Service**: Full implementation of the `envoy.service.ext_proc.v3.ExternalProcessor` gRPC contract.
- **Synchronous Message Handling**:
    - **Request Headers**: Efficient classification and recording of request header metadata.
    - **Response Headers**: Classification and recording of response header metadata.
    - **Response Body**: Handling of response body chunks.
- **Stateless Continuation**: Automatically returns a `ProcessingResponse` with a "continue" instruction (zero mutations) for all supported message types, ensuring minimal latency overhead.
- **Resilience**: Unsupported message variants are handled as safe no-ops, preventing stream disruptions.

## APG Stage Behavior

Stage behavior contract:
- Stage input is mutable `ApgTransformContext&`.
- Stage output is `StageOutput{ result, note }`.
- `StageResult::Continue` executes the next stage.
- `StageResult::Error` stops the pipeline immediately and returns that exact output.
- `StageResult::SkipRemaining` stops the pipeline immediately (non-error flow) and
  returns that exact output.
- `note` is non-owning (`const char*`) and is propagated unchanged.

Pipeline trace behavior:
- Trace fields live in context: `trace_buffer`, `trace_capacity`, `trace_length`.
- Trace is reset at pipeline start.
- One token is appended per executed stage:
  `C` (Continue), `E` (Error), `S` (SkipRemaining).
- Trace writes are skipped when buffer is null or capacity is zero.
- Trace writes are safely truncated and never write out-of-bounds.

## Route Policy

ByteTaper uses a static YAML-driven policy framework to define how specific routes should be optimized. Policies are loaded at startup and enforced by the Envoy ExtProc adapter.

### Features

- **YAML Configuration**: Human-readable policy definitions using the `yaml-cpp` library.
- **Request Matching**:
  - **Path Matching**: Support for `prefix` and `exact` path matching logic.
  - **Method Filtering**: Filter policies by HTTP method (GET, POST, PUT, DELETE, PATCH, or Any).
- **Optimization Primitives**:
  - **Field Filtering**: Define `allowlist` or `denylist` rules for JSON response body fields.
  - **Response Size Limits**: Enforce a `max_response_bytes` cap to reject or truncate oversized payloads.
- **Cache Control**: Declarative `cache` placeholders for future-ready caching behavior (bypass/store/TTL).
- **Validation Tooling**: Standalone `bytetaper-validate-policy` CLI for pre-deployment configuration verification.

### Policy Example

```yaml
routes:
  - id: "api-v1-proxy"
    match:
      kind: "prefix"
      prefix: "/api/v1/"
    method: "get"
    mutation: "disabled"
    max_response_bytes: 1048576 # 1 MiB
    cache:
      behavior: "store"
      ttl_seconds: 300
    field_filter:
      mode: "allowlist"
      fields:
        - "id"
        - "name"
        - "status"
```

### Policy Validation

Validate your policy file before deployment using the built-in validator:

```bash
# Using Docker (recommended)
docker compose run --rm bytetaper-build build/bytetaper-validate-policy examples/policy/bytetaper-policy.yaml
```

The tool will print a detailed per-route report on success or a descriptive error message on failure.

#### Exit Codes
- `0`: Success (all routes valid)
- `1`: Usage error
- `2`: YAML parse/load failure
- `3`: Validation rule violation (e.g., missing ID, invalid path prefix)

### Documentation

For more details on matching logic and configuration options, see [Route Policy Reference](docs/route-policy.md).

## Field Selection

Field Selection is a core ByteTaper primitive designed to reduce API payload sizes by selectively including or excluding JSON response fields. By stripping unnecessary data at the edge, ByteTaper reduces egress costs and improves client processing performance without requiring backend modifications.

### Capabilities

- **Nested Path Support**: Target data within complex structures using **dotted notation** (`user.id`) or **array notation** (`items[]`).
- **Allowlist Mode**: Explicitly select which fields to **keep**. Any field not in the list is automatically removed from the response body.
- **Denylist Mode**: Select which fields to **remove**. Ideal for stripping sensitive or internal-only fields while keeping the rest of the response intact.

### Technical Design & Performance

The Field Selection engine is built with strict **Orthodox C++** principles to ensure zero latency impact on the hot path:

- **Streaming Single-Pass**: Data is filtered and written to the output buffer in a single pass without intermediate object creation.
- **Zero Heap Allocation**: All parsing, path tracking, and matching logic use fixed-capacity flat buffers. This eliminates memory fragmentation and unpredictable allocation latency jitter.
- **Predictable O(N) Complexity**: Performance scales linearly with the size of the response body.

### Constraints

To maintain high performance and predictable memory usage, the following limits apply:
- **Max Selection Path**: Path strings are capped at 512 characters.
- **Max Field Rules**: 16 field rules can be defined per route.
- **JSON Only**: Field selection operates on valid UTF-8 JSON response bodies.

### Example Configuration

```yaml
field_filter:
  mode: "allowlist"
  fields:
    - "id"
    - "user.display_name"
    - "items[].price"
```

### Documentation

For a detailed behavior reference and example scenarios, see [Field Selection Reference](docs/field-selection.md).

## Observability

ByteTaper provides comprehensive observability through Prometheus metrics and HTTP response headers. These tools allow you to monitor the performance, savings, and safety status of your JSON optimizations.

See [Observability Guide](docs/observability.md) for a complete reference on monitoring ByteTaper.

## Policy Safety

See [Policy Safety](docs/policy-safety.md) for a complete reference on policy safety.

## Cache Policy

See [Cache Policy](docs/cache-policy.md) for a complete reference on tiered caching.

## Pagination Policy

See [Pagination Policy](docs/pagination-policy.md) for a complete reference on pagination handling.

## Compression Policy

See [Compression Policy](docs/compression-policy.md) for a complete reference on response compression handling.

## Coalescing Policy

See [Request Coalescing](docs/coalescing-policy.md) for a complete reference on request coalescing.

## Variant Caching Policy

See [Materialized Field-Filtered Variant Cache](docs/materialized-filed-filtered-variant-cache.policy) for a complete reference on variant caching.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for details on our development process and how to contribute.

## License

This tool is licensed under:

- `AGPL-3.0-only`, or
- `LicenseRef-Commercial`

See repository license files and source SPDX headers for details.
See `LICENSES/` for full license texts.

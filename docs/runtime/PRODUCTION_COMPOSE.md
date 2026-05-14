# Production Compose Deployment Guide

This document outlines the operational procedures for deploying ByteTaper in production using the standalone `docker-compose.prod.yml` specification, and detailing the automated runtime smoke validation matrix.

## Production vs. Development Compose Architecture

ByteTaper maintains complete architectural separation between development and production compose stacks:

| Feature / Configuration | `docker-compose.yml` (Development) | `docker-compose.prod.yml` (Production) |
|---|---|---|
| **Target Image** | Multi-stage builder with compiler / dev tools | Minimal production runtime image (`UID 1001`) |
| **Root Filesystem** | Writable root filesystem | `read_only: true` with strict tmpfs mounts |
| **Volume Mounts** | Source code bind mounted into container | Read-only policy file + Named L2 cache volume |
| **Admin Port (18082)**| Exposed for interactive debugging | Completely disabled / unexposed |
| **Health Check** | HTTP `/healthz` | HTTP `/readyz` with startup grace period |
| **Capabilities** | Default container capabilities | `cap_drop: ALL` + `no-new-privileges:true` |

---

## Deployment Steps

### 1. Build the Production Candidate Image
Ensure the image is built with precise release metadata:

```bash
docker build \
  -f docker/production.Dockerfile \
  --build-arg BYTETAPER_VERSION=v1.0.0 \
  --build-arg BYTETAPER_GIT_SHA="$(git rev-parse HEAD)" \
  --build-arg "BYTETAPER_BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  -t bytetaper-runtime:latest \
  .
```

### 2. Launch Production Compose Stack
Specify the target image and spin up the stack in detached mode:

```bash
BYTETAPER_IMAGE=bytetaper-runtime:latest docker-compose -f docker-compose.prod.yml up -d
```

### 3. Verify Operational Health & Metrics
Evaluate readiness and metrics endpoints:

```bash
curl -fsS http://localhost:18081/healthz
curl -fsS http://localhost:18081/readyz
curl -fsS http://localhost:18081/metrics | grep bytetaper_cache
```

### 4. Restart Service
To gracefully restart the production gateway process without recreating volume mounts:

```bash
docker-compose -f docker-compose.prod.yml restart bytetaper
```

### 5. Teardown & Cleanup
To bring down the gateway and purge associated L2 cache storage volumes:

```bash
docker-compose -f docker-compose.prod.yml down -v
```

---

## Admin Endpoint Configuration (Opt-In)

By default, the production specification completely unexposes port `18082` and drops `--admin-enable-taperquery`. To temporarily enable administrative debugging, pass an override or custom command arguments:

```yaml
# Example override snippet for troubleshooting
services:
  bytetaper:
    ports:
      - "18080:18080"
      - "18081:18081"
      - "18082:18082"
    command:
      - "--listen-address=0.0.0.0:18080"
      - "--policy-file=/etc/bytetaper/policy.yaml"
      - "--l2-cache-path=/var/lib/bytetaper/l2-cache"
      - "--metrics-address=0.0.0.0"
      - "--metrics-port=18081"
      - "--admin-enable-taperquery"
```

---

## Runtime Smoke Validation Matrix

To validate runtime contract compliance, execute the automated 11-case smoke matrix against any running production candidate:

```bash
mkdir -p dist/runtime-smoke
tests/runtime/smoke_runtime_matrix.sh bytetaper-runtime:latest
```

The matrix systematically verifies:
1. HTTP Liveness (`/healthz`) and Readiness (`/readyz`).
2. Prometheus Cache Metrics format (`/metrics`).
3. Non-root execution identity (`UID 1001`).
4. Strict read-only root filesystem enforcement.
5. Inability to overwrite mounted policy files or system paths (`/opt/bytetaper`, `/usr/local/bin`).
6. L2 Cache volume write persistence across container restarts.
7. Total disablement of admin endpoints under default flags.
8. Safe failure and crash-looping when provided invalid policy YAML.

Upon completion, detailed diagnostic bundles and container state dumps are aggregated in `dist/runtime-smoke/`.

---

## Common Failures and Fixes

### 1. Port Conflict (`address already in use`)
* **Symptom:** Container fails to start with `bind: address already in use` for ports `18080` or `18081`.
* **Cause:** A legacy development compose stack (`docker-compose.yml`) or another local service is actively bound to those ports.
* **Fix:** Teardown orphaned or conflicting containers:
  ```bash
  docker-compose down -v --remove-orphans
  ```

### 2. Readiness Check Timeout (`Container failed to become ready`)
* **Symptom:** Container starts but `/readyz` never returns `200 OK`.
* **Cause:** Malformed YAML syntax in mounted policy files or unreadable policy file permissions.
* **Fix:** Check container startup error logs:
  ```bash
  docker-compose -f docker-compose.prod.yml logs bytetaper
  ```

### 3. Read-Only Filesystem Denials
* **Symptom:** Dynamic runtime operations fail with `Read-only file system` errors in logs.
* **Cause:** Process attempting to write ephemeral data outside of `/tmp` or `/var/run/bytetaper`.
* **Fix:** Ensure any new storage paths are explicitly declared as tmpfs mounts or named volumes in `docker-compose.prod.yml`.

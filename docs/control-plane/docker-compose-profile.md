# Docker Compose Control Plane + Runtime Profile

This profile runs ByteTaper in a separated Control Plane + Runtime topology for local smoke validation.

## Topology

```text
curl / CI
  -> bytetaper-control-plane :19090 (mutation + fleet status)
  -> RocksDB volume bytetaper-policy-state

Client
  -> envoy :10000
  -> bytetaper-runtime :18080 (ext_proc only)
  -> mock-api :8080
```

Envoy **must** point at `bytetaper-runtime`, not the control plane service.

## Start

```bash
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) \
  docker compose -f docker-compose.yml -f docker-compose.control-plane.yml up -d --build \
  bytetaper-control-plane bytetaper-runtime mock-api envoy
```

Use `docker-compose` instead of `docker compose` if your install only provides the standalone binary.

The overlay:

- Clears monolithic `bytetaper-extproc` host ports (`ports: !reset []`) so it does not claim **18080**/**18081**.
- Publishes runtime metrics on host **18083** → container **18081** (avoids extproc port collision).
- Compose test scripts poll runtime `/readyz` **inside** the `bytetaper-runtime` container (not host `:18081` alone).
- Overrides Envoy `depends_on`: `bytetaper-extproc` is only `service_started` (not healthy); **`bytetaper-runtime`** and **`mock-api`** must be healthy before Envoy starts.

Envoy’s demo config targets **`bytetaper-runtime:18080`** for ext_proc (not control plane or monolithic extproc).

Or run the scripted smoke:

```bash
chmod +x scripts/demo/control-plane-demo.sh
./scripts/demo/control-plane-demo.sh
```

## Key endpoints

| Service | Port | Purpose |
|---------|------|---------|
| `bytetaper-control-plane` | 19090 | `GET /admin/control-plane/policy/current`, `POST /admin/control-plane/policy/apply`, `GET /admin/control-plane/fleet/status` |
| `bytetaper-control-plane` | 19091 | Metrics `/healthz`, `/readyz` (mapped from container 18081) |
| `bytetaper-runtime` | 18080 | ext_proc gRPC |
| `bytetaper-runtime` | 18083 | Metrics (`/readyz`, `/healthz`; maps to container 18081) |
| `envoy` | 10000 | HTTP ingress |

## Environment (control plane)

| Variable | Value in profile |
|----------|------------------|
| `BYTETAPER_RUNTIME_ROLE` | `control-plane` |
| `BYTETAPER_POLICY_STATE_DB` | `/var/lib/bytetaper/policy-state-db` |

## Environment (runtime)

| Variable | Value in profile |
|----------|------------------|
| `BYTETAPER_RUNTIME_ROLE` | `runtime-only` |
| `BYTETAPER_DEPLOYMENT_MODE` | `multi-runtime` |
| `BYTETAPER_CONTROL_PLANE_ENDPOINT` | `http://bytetaper-control-plane:19090` |
| `BYTETAPER_POLICY_PULL_ENABLED` | `1` |
| `BYTETAPER_POLICY_STATE_DIR` | `/var/lib/bytetaper/policies` (last-known-good mirror only) |

## Manual smoke

```bash
curl -fsS http://localhost:19090/admin/control-plane/policy/current | jq .
curl -fsS -X POST http://localhost:19090/admin/control-plane/policy/apply \
  -H 'content-type: application/json' \
  -d @examples/taperquery/apply-policy.json | jq .
curl -fsS http://localhost:19090/admin/control-plane/fleet/status | jq .
curl -fsS http://localhost:10000/api/v1/small | jq .
```

## Testing

| Tier | Command |
|------|---------|
| Unit | `make test-control-plane-unit` or `./scripts/test/control-plane-unit.sh` |
| Integration | `make test-control-plane-integration` |
| Compose smoke | `make test-control-plane-compose` or `./scripts/test/control-plane-compose.sh` |
| Field-allowlist E2E only | `docker compose -f docker-compose.yml -f docker-compose.control-plane.yml run --rm bytetaper-control-plane-compose-test` |

Regression mapping: [test-matrix.md](test-matrix.md).

## Reset persistent state

```bash
docker compose -f docker-compose.yml -f docker-compose.control-plane.yml down -v
```

Scripts and compose tests call `down -v` on exit to avoid stale RocksDB / mirror state.

## Failure smoke (manual)

1. Stop control plane: `docker compose ... stop bytetaper-control-plane`
2. Verify runtime continues serving last-known-good policy via Envoy.
3. Start control plane again; runtime should reconnect and report status.
4. Tamper the runtime mirror by restoring a self-consistent **stale** generation (`G-1`) from `GET /admin/control-plane/policy/version`, including the matching `versions/…` file referenced by `versionedPolicyFile`. Assert runtime starts, fleet reports stale/divergent before pull reconverges, and CP does not promote the stale mirror to a new committed generation.

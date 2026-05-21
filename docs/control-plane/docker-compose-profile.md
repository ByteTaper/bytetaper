# Docker Compose Control Plane + Runtime Profile

Documentation hub: [index.md](index.md).

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

Prerequisites: profile running (see [Start](#start)).

```bash
COMPOSE="docker compose -f docker-compose.yml -f docker-compose.control-plane.yml"
CP=http://localhost:19090

curl -fsS "${CP}/admin/control-plane/policy/current" | jq .

# Apply: CAS from current committed pointer (not a fixed apply-policy.json)
CURRENT="$(curl -fsS "${CP}/admin/control-plane/policy/current")"
GEN="$(echo "${CURRENT}" | jq -r '.generation')"
PID="$(echo "${CURRENT}" | jq -r '.policy_id')"
jq -n \
  --rawfile source examples/taperquery/apply-policy-source.yaml \
  --arg request_id "cp-manual-apply-1" \
  --arg operator_id "control-plane-manual" \
  --argjson expected_base_generation "${GEN}" \
  --arg expected_base_policy_id "${PID}" \
  '{source: $source, request_id: $request_id, operator_id: $operator_id,
    expected_base_generation: $expected_base_generation,
    expected_base_policy_id: $expected_base_policy_id}' \
  | curl -fsS -X POST "${CP}/admin/control-plane/policy/apply" \
      -H 'content-type: application/json' -d @- | jq .

curl -fsS "${CP}/admin/control-plane/fleet/status" | jq .
curl -fsS http://localhost:10000/api/v1/small | jq .
```

Or run `./scripts/demo/control-plane-demo.sh`, which uses the same CAS pattern.

After apply, wait until `fleet.converged` is `true` before restart checks below.

## Restart durability

Validates that committed policy and the runtime last-known-good (LKG) mirror survive process restarts. Automated equivalent: `make test-control-plane-compose` (restart block in [`control-plane-compose.sh`](../../scripts/test/control-plane-compose.sh)) and [`control-plane-demo.sh`](../../scripts/demo/control-plane-demo.sh).

### Runtime restart check

Confirms the runtime reloads its LKG mirror on startup and re-converges to the Control Plane committed generation (pull loop + fleet status).

```bash
COMPOSE="docker compose -f docker-compose.yml -f docker-compose.control-plane.yml"

# 1. Record committed generation after apply + fleet converged
GEN="$(curl -fsS http://localhost:19090/admin/control-plane/policy/current | jq -r '.generation')"
POLICY_ID="$(curl -fsS http://localhost:19090/admin/control-plane/policy/current | jq -r '.policy_id')"
echo "committed before restart: generation=${GEN} policy_id=${POLICY_ID}"

# 2. Restart runtime only
${COMPOSE} restart bytetaper-runtime

# 3. Wait for runtime healthy + ready (prefer in-container probe; host fallback on :18083)
for i in $(seq 1 60); do
  if ${COMPOSE} exec -T bytetaper-runtime bash -c \
    'exec 3<>/dev/tcp/127.0.0.1/18081 && printf "GET /readyz HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" >&3 && read -r line <&3 && echo "$line" | grep -q 200'; then
    break
  fi
  sleep 2
done
curl -fsS http://127.0.0.1:18083/readyz   # host-mapped metrics port

# 4. LKG: data path still serves without CP restart
curl -fsS http://localhost:10000/api/v1/small | jq .

# 5. Re-converge: committed generation unchanged; fleet converged
curl -fsS http://localhost:19090/admin/control-plane/fleet/status | jq '.fleet.converged, .committed, .runtimes'
GEN_AFTER="$(curl -fsS http://localhost:19090/admin/control-plane/policy/current | jq -r '.generation')"
test "${GEN_AFTER}" = "${GEN}" && echo "PASS: generation ${GEN} unchanged after runtime restart"
```

Expected:

- Envoy returns `200` on `/api/v1/small` immediately after runtime restart (LKG snapshot).
- `policy/current` `generation` and `policy_id` match pre-restart values.
- `fleet.converged` is `true` and per-runtime `convergence_status` is `Converged`.

### Control Plane + runtime restart check

Optional combined restart (matches compose smoke and demo script):

```bash
COMPOSE="docker compose -f docker-compose.yml -f docker-compose.control-plane.yml"
GEN="$(curl -fsS http://localhost:19090/admin/control-plane/policy/current | jq -r '.generation')"

${COMPOSE} restart bytetaper-control-plane bytetaper-runtime

# CP health
curl -fsS http://127.0.0.1:19091/healthz

# Runtime ready + data path (same probes as runtime-only restart above)
curl -fsS http://127.0.0.1:18083/readyz
curl -fsS http://localhost:10000/api/v1/small | jq .

# Committed state and fleet unchanged
curl -fsS http://localhost:19090/admin/control-plane/policy/current | jq .
curl -fsS http://localhost:19090/admin/control-plane/fleet/status | jq '.fleet.converged, .committed.generation'
```

Expected: RocksDB committed state survives CP restart; runtime LKG + pull restore convergence without generation regression.

## Testing

| Tier | Command |
|------|---------|
| Unit | `make test-control-plane-unit` or `./scripts/test/control-plane-unit.sh` |
| Integration | `make test-control-plane-integration` |
| Compose smoke | `make test-control-plane-compose` or `./scripts/test/control-plane-compose.sh` (cleans volumes, waits for `bytetaper-build-server` with bounded timeout, then starts CP/runtime/Envoy; prints diagnostics on failure) |
| Field-allowlist E2E only | `docker compose -f docker-compose.yml -f docker-compose.control-plane.yml run --rm bytetaper-control-plane-compose-test` |

Regression mapping: [test-matrix.md](test-matrix.md).

## Reset persistent state

```bash
docker compose -f docker-compose.yml -f docker-compose.control-plane.yml down -v
```

Scripts and compose tests call `down -v` on exit to avoid stale RocksDB / mirror state.

## Failure smoke (manual)

Run after [Restart durability](#restart-durability) succeeds. Record `GEN`, `policy_id`, and `canonical_hash` from `policy/current` before step 1.

1. **Control Plane stop:** `docker compose -f docker-compose.yml -f docker-compose.control-plane.yml stop bytetaper-control-plane`
2. **LKG while CP down:** Verify runtime continues serving last-known-good policy via Envoy (`curl http://localhost:10000/api/v1/small`).
3. **Control Plane start:** `docker compose ... start bytetaper-control-plane` — wait for CP health; runtime should reconnect, report status, and fleet should reconverge (`fleet.converged: true`, committed generation still `GEN`).
4. **Mirror tamper:** Restore a self-consistent **stale** generation (`GEN - 1`) from `GET /admin/control-plane/policy/version?resource_key=policy/default/runtime&generation=<G-1>` into the runtime mirror (`versions/…` + metadata). Restart runtime (`docker compose ... restart bytetaper-runtime`), wait for ready, poll fleet until converged. Assert CP committed pointer is still `GEN` (no auto-promotion of stale mirror). See [versioned-policy-history.md](versioned-policy-history.md).

Automated coverage: `control-plane-compose.sh` failure flow after the restart-durability block.

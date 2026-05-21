# Control Plane API Index

## Two API surfaces

ByteTaper exposes policy operations through two surfaces. Operators and integrators must use the correct one.

| Surface | Access | Scope |
|---------|--------|-------|
| **HTTP admin** | `http://<cp-host>:19090/admin/control-plane/*` | Production operator path for apply, query, fleet |
| **In-process `ControlPlaneService`** | C++ service API, integration tests | Full contract including dry-run, diff, rollback, manual resolution |

> **Note:** Dry-run, diff, rollback plan/execute, repair-local, adopt-local, and job query are implemented on `ControlPlaneService` but are **not** yet exposed as HTTP routes. Documented here for integrators and test authors; HTTP may be added later.

## HTTP endpoints (implemented)

Base URL (compose): `http://localhost:19090`

| Method | Path | Doc |
|--------|------|-----|
| `GET` | `/healthz` | Process health |
| `GET` | `/readyz` | Readiness |
| `GET` | `/admin/control-plane/policy/current` | [apply-api.md](apply-api.md) |
| `GET` | `/admin/control-plane/policy/version?generation=N` | [versioned-policy-history.md](versioned-policy-history.md) |
| `POST` | `/admin/control-plane/policy/apply` | [apply-api.md](apply-api.md) |
| `GET` | `/admin/control-plane/fleet/status` | [fleet-status-api.md](fleet-status-api.md) |
| `POST` | `/admin/control-plane/runtime/status` | [fleet-status-api.md](fleet-status-api.md) |

Implementation: [`src/admin/control_plane_admin_http_server.cpp`](../../src/admin/control_plane_admin_http_server.cpp).

## Resource key

GET endpoints (`policy/current`, `policy/version`, `fleet/status`) accept an optional query parameter (default `policy/default/runtime`):

```text
?resource_key=policy/default/runtime
```

The HTTP server parses **`resource_key` only** in query strings (`parse_resource_key_query` in `control_plane_admin_http_server.cpp`). `resourceKey` in a query string is ignored and the default key is used.

`POST /admin/control-plane/runtime/status` accepts `resource_key` or `resourceKey` in the JSON body.

## Authentication

| Variable | Description |
|----------|-------------|
| `BYTETAPER_CONTROL_PLANE_TOKEN` | Static bearer token for mutation and sensitive reads (when auth enabled) |
| Header | `Authorization: Bearer <token>` |

Tokens are **never** written to logs. See [security.md](security.md).

Production mode rejects mutations without valid auth and rejects unsafe public bind unless `allow_public_bind=true`. See [deployment-guardrails.md](deployment-guardrails.md).

## In-process service API

`ControlPlaneService` ([`control_plane_service.h`](../../include/control_plane/control_plane_service.h)):

| Method | Purpose | HTTP today |
|--------|---------|------------|
| `dry_run` | Validate candidate without commit | No |
| `apply` | Submit async apply job | Yes (`POST .../apply`) |
| `diff` | Route-level diff plan | No |
| `plan_rollback` / `rollback` | Rollback preview and execute | No |
| `get_active_policy` | Active pointer | Yes (`GET .../current`) |
| `get_policy_version` | Version by generation | Yes (`GET .../version`) |
| `list_policy_versions` | List generations | No |
| `get_policy_update_job` | Job status by id | No |
| `report_runtime_status` | Runtime heartbeat | Yes (`POST .../runtime/status`) |
| `get_fleet_status` | Fleet convergence | Yes (`GET .../fleet/status`) |
| `plan_repair_local` / `repair_local` | Push committed policy to runtime | No |
| `plan_adopt_local` / `adopt_local` | Promote local divergent policy | No |

Details: [apply-api.md](apply-api.md), [fleet-status-api.md](fleet-status-api.md), [manual-resolution-api.md](manual-resolution-api.md).

## Runtime-local TaperQuery admin (single-node)

For single-process local dev, `/admin/taperquery/*` on port `18082` remains available. **Do not** use it for multi-runtime committed policy.

See [TaperQuery Admin HTTP](../taperquery/admin-http.md).

## Status and error codes

Apply and manual operations return `PolicyApplyStatus` string values (e.g. `Accepted`, `RejectedCasMismatch`). See [`policy_apply_status.h`](../../include/control_plane/policy_apply_status.h).

Manual resolution uses additional `error_code` strings (e.g. `ROLLBACK_CONFIRMATION_REQUIRED`). See [manual-resolution-api.md](manual-resolution-api.md).

## Examples

```bash
CP=http://localhost:19090

# Active policy (optional non-default key)
curl -fsS "${CP}/admin/control-plane/policy/current?resource_key=policy/default/runtime" | jq .

# Apply with CAS from current pointer (see apply-api.md)
CURRENT="$(curl -fsS "${CP}/admin/control-plane/policy/current")"
GEN="$(echo "${CURRENT}" | jq -r '.generation')"
PID="$(echo "${CURRENT}" | jq -r '.policy_id')"
jq -n \
  --rawfile source examples/taperquery/apply-policy-source.yaml \
  --argjson expected_base_generation "${GEN}" \
  --arg expected_base_policy_id "${PID}" \
  '{source: $source, request_id: "cp-demo-apply-1", operator_id: "control-plane-demo",
    expected_base_generation: $expected_base_generation,
    expected_base_policy_id: $expected_base_policy_id}' \
  | curl -fsS -X POST "${CP}/admin/control-plane/policy/apply" \
      -H 'content-type: application/json' -d @- | jq .

# Fleet
curl -fsS "${CP}/admin/control-plane/fleet/status?resource_key=policy/default/runtime" | jq .
```

Compose profile: [docker-compose-profile.md](docker-compose-profile.md). Demo: [`scripts/demo/control-plane-demo.sh`](../../scripts/demo/control-plane-demo.sh).

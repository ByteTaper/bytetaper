# Fleet Status and Runtime Status API

## Overview

Fleet status aggregates committed policy with per-runtime convergence classification. Runtimes report local activation state via periodic status POSTs; the Control Plane classifies each runtime as converged, stale, divergent, failed, degraded, or unreachable.

## HTTP: GET `/admin/control-plane/fleet/status`

Optional query: `resource_key` (default `policy/default/runtime`). Use snake_case in the query string; `resourceKey` is not parsed on GET endpoints.

### Response (success)

```json
{
  "ok": true,
  "resource_key": "policy/default/runtime",
  "committed": {
    "generation": 3,
    "policy_id": "...",
    "canonical_hash": "sha256:..."
  },
  "fleet": {
    "runtime_count": 1,
    "converged_count": 1,
    "stale_count": 0,
    "converged": true
  },
  "runtimes": [
    {
      "runtime_id": "runtime-1",
      "active_generation": 3,
      "active_policy_id": "...",
      "active_canonical_hash": "sha256:...",
      "activation_status": "active",
      "convergence_status": "Converged",
      "control_plane_reachable": true
    }
  ]
}
```

### Fleet summary fields

| Field | Description |
|-------|-------------|
| `runtime_count` | Registered runtimes reporting status |
| `converged_count` | Runtimes matching committed generation/hash |
| `stale_count` | Behind committed generation |
| `converged` | Fleet-level boolean (all required runtimes converged) |

Additional counters exist in the full service model (`divergent_count`, `failed_count`, `unreachable_count`, `degraded_count`) — see [fleet-convergence.md](fleet-convergence.md).

### Per-runtime fields

| Field | Description |
|-------|-------------|
| `runtime_id` | Stable runtime identity |
| `active_generation` | Generation runtime believes is active |
| `active_policy_id` / `active_canonical_hash` | Local active identity |
| `activation_status` | e.g. `active`, `activation_failed` |
| `convergence_status` | `Converged`, `Stale`, `Divergent`, `Failed`, `Degraded`, `Unreachable`, `Unknown` |
| `control_plane_reachable` | Runtime could reach CP on last report |

Timestamps (`lastPullAtUnixEpochMs`, `lastActivatedAtUnixEpochMs`) are parsed from runtime status reports in the service layer; fleet JSON may omit them in minimal HTTP responses.

### Error response

```json
{
  "ok": false,
  "error": "...",
  "error_code": "FLEET_STATUS_ACTIVE_POINTER_MISSING"
}
```

## HTTP: POST `/admin/control-plane/runtime/status`

Runtimes (or test harnesses) push status. Body accepts snake_case or camelCase:

```json
{
  "runtime_id": "bytetaper-runtime",
  "resource_key": "policy/default/runtime",
  "gateway_adapter": "envoy-extproc",
  "active_generation": 3,
  "active_policy_id": "...",
  "active_canonical_hash": "sha256:...",
  "activation_status": "active",
  "control_plane_reachable": true,
  "data_path_mode": "serving",
  "last_pull_at_unix_epoch_ms": 1779170000000,
  "last_activated_at_unix_epoch_ms": 1779170000000
}
```

### Response

```json
{
  "ok": true,
  "error": "",
  "error_code": ""
}
```

Validation errors: `RUNTIME_STATUS_INVALID`, `RUNTIME_STATUS_RESOURCE_UNKNOWN`.

## Convergence classification

Classification logic: `classify_runtime_convergence()` in [`runtime_convergence_status.h`](../../include/control_plane/runtime_convergence_status.h).

| Status | Meaning |
|--------|---------|
| `Converged` | Matches committed generation and canonical hash |
| `Stale` | Behind committed generation |
| `Divergent` | Local identity does not match committed |
| `Failed` | Activation failed |
| `Degraded` | Serving with issues (config-dependent) |
| `Unreachable` | No recent status / pull |
| `Unknown` | Insufficient data |

Error codes (when applicable): `RUNTIME_STATUS_POLICY_MISMATCH`, `RUNTIME_STATUS_STALE`, `RUNTIME_STATUS_UNREACHABLE`.

See [fleet-convergence.md](fleet-convergence.md).

## Operator workflow

```bash
# After apply
curl -fsS http://localhost:19090/admin/control-plane/fleet/status | jq .

# Wait for convergence (compose script)
# poll_fleet_converged in scripts/test/_control_plane_compose_lib.sh
```

Fleet metrics: `bytetaper_control_plane_fleet_*` — [observability.md](observability.md).

## Service API

`ControlPlaneService::get_fleet_status(resource_key)` returns full `FleetPolicyStatus` including observability block (`last_apply_job_id`, `queue_depth`, `cleanup_pending`, etc.).

`report_runtime_status(report)` updates fleet registry.

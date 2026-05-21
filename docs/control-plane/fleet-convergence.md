# Fleet Convergence

## Overview

Fleet convergence describes whether all registered runtimes are serving the **same committed generation and canonical hash** as the Control Plane active pointer.

Classification: [`RuntimeConvergenceStatus`](../../include/control_plane/runtime_convergence_status.h), `classify_runtime_convergence()`.

## RuntimeConvergenceStatus

| Status | Meaning | Operator action |
|--------|---------|-----------------|
| `Converged` | Matches committed generation + hash | None |
| `Stale` | Behind committed generation | Wait for pull, or investigate stuck pull |
| `Divergent` | Local identity ≠ committed | Manual resolution — no auto-repair |
| `Failed` | Activation failed | Check lifecycle logs; repair or rollback |
| `Degraded` | Serving with degraded mode | Review `data_path_mode` and errors |
| `Unreachable` | No recent status report | Network, runtime down, or CP partition |
| `Unknown` | Insufficient telemetry | Verify runtime status POST enabled |

## Error codes

| Code | When |
|------|------|
| `RUNTIME_STATUS_POLICY_MISMATCH` | Divergent identity |
| `RUNTIME_STATUS_STALE` | Stale behind committed |
| `RUNTIME_STATUS_UNREACHABLE` | Unreachable classification |

Returned via `convergence_status_error_code()` for applicable statuses.

## Fleet-level summary

`GET /admin/control-plane/fleet/status` returns:

```json
"fleet": {
  "runtime_count": 1,
  "converged_count": 1,
  "stale_count": 0,
  "converged": true
}
```

Service-layer `FleetPolicyStatus` also tracks `divergent_count`, `failed_count`, `unreachable_count`, `degraded_count`.

## Committed vs runtime active

| Source | Fields |
|--------|--------|
| `committed` | Control Plane active pointer |
| `runtimes[].active_*` | Last reported runtime state |

Convergence requires runtime `active_generation` and `active_canonical_hash` to match `committed`.

## Stale mirror tamper (regression scenario)

When a test or operator restores generation *G-1* into the runtime mirror:

1. Runtime may start and briefly serve stale content (LKG)
2. Fleet reports `Stale` or `Divergent`
3. Pull loop fetches generation *G* and reconverges
4. Control Plane must **not** auto-promote stale mirror to new committed generation

See [versioned-policy-history.md](versioned-policy-history.md) and [test-matrix.md](test-matrix.md) §9.

## Metrics

Prometheus (when scraped):

- `bytetaper_control_plane_fleet_runtime_count`
- `bytetaper_control_plane_fleet_converged_count`
- `bytetaper_control_plane_fleet_stale_count`
- `bytetaper_control_plane_fleet_failed_count`
- `bytetaper_control_plane_fleet_unreachable_count`
- `bytetaper_control_plane_fleet_degraded_count`
- `bytetaper_control_plane_fleet_converged`

See [observability.md](observability.md).

## Lifecycle events

| Event | When |
|-------|------|
| `RuntimeConverged` | Runtime matched committed |
| `RuntimeStale` | Classified stale |
| `RuntimeActivationFailed` | Activation failed |
| `PolicyMismatchDetected` | Mismatch detected |

## Related docs

- [fleet-status-api.md](fleet-status-api.md)
- [runtime-pull-loop.md](runtime-pull-loop.md)
- [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md)

# Runtime Policy Pull Loop

## Overview

In Control Plane + Runtime deployments, runtime pods **pull** committed policy from the Control Plane HTTP API rather than mutating global state locally. The pull loop drives activation when committed generation or canonical hash changes.

## Configuration

| Variable | Description |
|----------|-------------|
| `BYTETAPER_RUNTIME_ROLE` | `runtime-only` — mutations rejected locally |
| `BYTETAPER_CONTROL_PLANE_ENDPOINT` | Base URL, e.g. `http://bytetaper-control-plane:19090` |
| `BYTETAPER_POLICY_PULL_ENABLED` | `1` to enable pull loop |
| `BYTETAPER_POLICY_PULL_INTERVAL_MS` | Poll interval (compose default: `2000`) |
| `BYTETAPER_POLICY_STATE_DIR` | Local LKG mirror directory |

Compose profile: [docker-compose-profile.md](docker-compose-profile.md).

## Pull sequence

```text
every BYTETAPER_POLICY_PULL_INTERVAL_MS:
  GET /admin/control-plane/policy/current
  if generation/hash changed from local active:
    fetch canonical YAML / IR
    run PolicyActivationBarrier
    update local LKG mirror
  POST /admin/control-plane/runtime/status
```

Client: [`control_plane_policy_client.cpp`](../../src/runtime_policy/control_plane_policy_client.cpp).

## Control Plane unavailable

| Scenario | Runtime behavior |
|----------|------------------|
| CP down, previously activated | Continue serving **last-known-good** snapshot |
| CP down, never activated | Bootstrap/LKG mirror rules apply; may fail closed |
| CP returns | Pull resumes; reconverge to committed generation |

Runtime does **not** commit local mirror state to Control Plane automatically when CP returns.

## Status reporting

Runtime POSTs `RuntimeStatusReport` with:

- `active_generation`, `active_policy_id`, `active_canonical_hash`
- `activation_status`, `control_plane_reachable`
- `last_pull_at_unix_epoch_ms`, `last_activated_at_unix_epoch_ms`

See [fleet-status-api.md](fleet-status-api.md).

## Convergence

After operator apply:

1. CP commits new generation
2. Pull loop detects change on each runtime
3. Activation barrier runs per runtime
4. Fleet `converged` becomes true when all runtimes match committed

Compose helper: `poll_fleet_converged` in [`_control_plane_compose_lib.sh`](../../scripts/test/_control_plane_compose_lib.sh).

## Failure smoke: CP stop

Compose regression stops Control Plane and verifies:

- Envoy data path still serves LKG policy
- CP restart → runtime reconnects and reports status

See [docker-compose-profile.md](docker-compose-profile.md) § Failure smoke.

## Related docs

- [runtime-policy-plane.md](runtime-policy-plane.md)
- [fleet-convergence.md](fleet-convergence.md)
- [activation-barrier.md](activation-barrier.md)

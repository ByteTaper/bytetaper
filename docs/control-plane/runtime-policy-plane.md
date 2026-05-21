# Runtime Policy Plane

## Overview

The Runtime Policy Plane consumes **committed** policy from the Control Plane, compiles it into immutable snapshots, and maintains a local last-known-good (LKG) mirror for resilience. It does not own global policy mutations.

Code: [`include/runtime_policy/`](../../include/runtime_policy/), pull client [`control_plane_policy_client`](../../src/runtime_policy/control_plane_policy_client.cpp).

## Responsibilities

| Owns | Must not |
|------|----------|
| Pull loop and activation requests | Write to `PolicyStateStore` |
| YAML/IR compile into `RuntimePolicySnapshot` | Depend on gateway-specific APIs |
| Local LKG mirror under `BYTETAPER_POLICY_STATE_DIR` | Auto-repair mismatch with committed state |
| Report runtime status to Control Plane | Apply global policy without CP path (in `runtime-only` role) |

## Pull loop

When `BYTETAPER_POLICY_PULL_ENABLED=1` and `BYTETAPER_CONTROL_PLANE_ENDPOINT` is set:

1. Poll `GET /admin/control-plane/policy/current` on interval (`BYTETAPER_POLICY_PULL_INTERVAL_MS`, default 2000ms compose).
2. Compare committed generation/hash to local active snapshot.
3. On change, fetch canonical policy and run operational activation barrier.
4. POST `/admin/control-plane/runtime/status` with local generation, hash, activation status.

See [runtime-pull-loop.md](runtime-pull-loop.md).

## Last-known-good mirror

Runtime persists mirror files for offline startup:

```text
BYTETAPER_POLICY_STATE_DIR/
  active-policy.yaml
  active-policy.meta.json
  versions/...
```

When Control Plane is unavailable, runtime continues serving the last successfully activated snapshot (LKG). It does **not** promote stale mirror content to committed state.

## Bootstrap vs committed

| Source | When |
|--------|------|
| Bootstrap file (`/etc/bytetaper/policy.yaml`) | CP store empty; one-time import to generation 1 |
| Committed pull | Normal operation after CP has active pointer |

Bootstrap overwrite when active policy exists requires operator confirmation.

## Mismatch detection

`classify_mismatch()` ([`policy_mismatch_classifier.h`](../../include/runtime_policy/policy_mismatch_classifier.h)) classifies local mirror health:

- `Active` — aligned with committed
- `InactiveStaleLocal`, `InactiveDivergedLocal`, `InactiveCorruptedLocal`, etc.

No automatic repair — [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md).

## Deployment role

`BYTETAPER_RUNTIME_ROLE=runtime-only` disables in-process TaperQuery mutation; all committed changes go through Control Plane HTTP.

See [deployment-guardrails.md](deployment-guardrails.md).

## Related docs

- [runtime-pull-loop.md](runtime-pull-loop.md)
- [activation-barrier.md](activation-barrier.md)
- [fleet-convergence.md](fleet-convergence.md)

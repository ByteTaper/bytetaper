# Operational Plane Synchronization

## Overview

The Operational Plane sits between committed policy (Control Plane / store) and the Data Plane execution path. It ensures a new generation is **safe to serve** before the active snapshot is swapped — coordinating cache epochs, L1/L2 namespaces, materialized variants, and cleanup queues.

**Core invariant:** `Persisted != Operationally Activated`

Code: [`include/operational/`](../../include/operational/), entry point [`PolicyActivationBarrier`](../../include/operational/policy_activation_barrier.h).

## Components

| Component | Role |
|-----------|-----|
| `PolicyActivationBarrier` | Orchestrates full activation sequence |
| `policy_operational_diff` | Computes route-level operational diff |
| `cache_namespace_sync` | Aligns L1 cache namespaces with new generation |
| `route_epoch_sync` | Bumps per-route cache epochs |
| `policy_cleanup_sync` | Enqueues L2 cleanup work |
| `materialized_variant_sync` | Invalidates materialized field variants |

## Activation flow

```text
Committed generation in store
        ↓
Operational diff (which routes changed)
        ↓
Cache namespace prepare
        ↓
Route epoch bump (immediate isolation)
        ↓
L2 / variant cleanup enqueued (may be async)
        ↓
Build RuntimePolicySnapshot
        ↓
Atomic snapshot swap on RuntimePolicyStore
        ↓
Activated — data path uses new generation
```

On failure, the barrier keeps the **old snapshot active** (`old_snapshot_still_active`) and emits `PolicyActivationFailed` lifecycle events.

## Relationship to Control Plane

| Plane | Action |
|-------|--------|
| Control Plane | Commits version + promotes active pointer |
| Operational Plane | Activates on runtime after pull or local apply |
| Data Plane | Reads only the swapped immutable snapshot |

The Control Plane does not bump route epochs or invalidate L1 — that is operational responsibility.

## HTTP pull path

When runtime has no local `PolicyStateStore` (typical `runtime-only`), activation uses:

- `committed_policy_ir` from pull response
- Prior snapshot’s `policy_ir` as `before_ir` for diff

See activation barrier implementation for store-null path.

## Related docs

- [activation-barrier.md](activation-barrier.md) — stage detail and statuses
- [cache-synchronization.md](cache-synchronization.md) — epoch and cleanup semantics
- [runtime-policy-plane.md](runtime-policy-plane.md) — who triggers activation

## Tests

`policy_activation_barrier_test.cpp` — field reduction epoch bump, swap ordering.

See [test-matrix.md](test-matrix.md) §3.

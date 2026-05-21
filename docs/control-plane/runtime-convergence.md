# Runtime Convergence (Architecture)

## Overview

Runtime convergence is the property that each runtime instance’s **operationally active** policy identity matches the Control Plane **committed** active pointer. This document describes the architectural model; API and operator detail live in [fleet-convergence.md](fleet-convergence.md).

## Model

```text
Control Plane active pointer (generation G, hash H)
              ↓ compare
Runtime status report (active_generation, active_canonical_hash)
              ↓ classify
RuntimeConvergenceStatus
              ↓ aggregate
Fleet converged boolean
```

## No auto-recovery

ByteTaper intentionally does **not**:

- Promote a stale local mirror to committed state
- Roll back committed state to match a divergent runtime
- Silently re-activate when mismatch is detected

Operators use [manual resolution](../runbooks/control-plane/manual-resolution.md): `repair_local`, `adopt_local`, `rollback`, or re-apply with correct CAS.

## CP unavailable

Convergence checks require CP availability for committed pointer and status ingestion. When CP is down:

- Runtimes may still **serve** LKG policy
- Fleet status is stale or unavailable
- Committed state does not change until CP returns

## Activation vs convergence

| Concept | Question answered |
|---------|-------------------|
| **Activation** | Is new generation safe to serve on this runtime? ([activation-barrier.md](activation-barrier.md)) |
| **Convergence** | Does this runtime’s active identity match CP committed? ([fleet-convergence.md](fleet-convergence.md)) |

A runtime can be activated locally but divergent if mirror tamper or partial failure occurred — fleet will show `Divergent` until resolved.

## Multi-runtime future

The same model extends to multiple runtimes and gateway adapters: each reports `runtime_id` and `gateway_adapter`; fleet aggregates counts per status.

## Related docs

- [fleet-convergence.md](fleet-convergence.md)
- [runtime-pull-loop.md](runtime-pull-loop.md)
- [architecture-boundary.md](architecture-boundary.md)

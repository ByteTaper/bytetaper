# Operational Activation Barrier

## Persisted ≠ Operationally Activated

A successful Control Plane commit means the generation is **persisted** and the active pointer is promoted. Runtimes do not immediately serve that generation on the data path until the **activation barrier** completes on each runtime instance.

This prevents old cache entries, stale route epochs, or prior snapshots from being reachable under a new policy identity.

## Activation stages

| Stage | Description |
|-------|-------------|
| **Committed** | Version stored; active pointer promoted in `PolicyStateStore` |
| **Operational diff computed** | `compute_operational_diff` — affected routes identified |
| **Cache namespace prepared** | L1 namespaces aligned with new generation |
| **Route epochs bumped** | Per-route epoch incremented — old entries logically isolated |
| **Cleanup enqueued** | L2 disk cleanup and materialized variant invalidation scheduled |
| **Snapshot built** | `RuntimePolicySnapshot` compiled from committed IR |
| **Snapshot swapped** | Atomic swap on `RuntimePolicyStore` |
| **Activated** | Data path serves new generation; readiness passes |

Implementation order in [`policy_activation_barrier.cpp`](../../src/operational/policy_activation_barrier.cpp) follows this coordination via `cache_namespace_sync`, `route_epoch_sync`, `policy_cleanup_sync`, `materialized_variant_sync`.

## PolicyApplyStatus (activation)

| Status | Meaning |
|--------|---------|
| `PolicyCommittedNotActivated` | Committed on CP; runtime not yet activated |
| `PolicyActivationInProgress` | Barrier running |
| `PolicyActivationFailed` | Barrier failed; prior snapshot may still serve |
| `PolicyActiveCleanupPending` | Activated; background cleanup still running |
| `PolicyActive` | Fully active |

## Failure behavior

On activation failure:

- `old_snapshot_still_active = true` when the previous snapshot remains the serving snapshot
- `PolicyActivationFailed` lifecycle event with `stage` and `error_code`
- Fleet may show `Failed` or `Divergent` until operator intervenes

Operators must not assume commit implies immediate data-path behavior — check fleet `activation_status` and convergence.

## Field-filter / allowlist changes

Field-level policy changes bump route cache epoch **before** swap so reduced allowlists cannot serve cached full-field responses. Test: `FieldReductionBumpsEpochBeforeSwap` in `policy_activation_barrier_test.cpp`.

## Serving safety vs async cleanup

| Concern | Timing |
|---------|--------|
| **Serving safety** | Immediate — epoch bump + snapshot swap before new policy is visible |
| **L2 / variant cleanup** | May complete asynchronously after swap |
| **Unreachable old entries** | Guaranteed by epoch isolation, not by waiting for L2 delete |

See [cache-synchronization.md](cache-synchronization.md).

## Lifecycle events

| Event | When |
|-------|------|
| `PolicyActivationStarted` | Barrier begins |
| `PolicyActivationSucceeded` | Swap complete |
| `PolicyActivationFailed` | Barrier failed |

Logged as structured JSON — [observability.md](observability.md).

## Related docs

- [operational-plane-sync.md](operational-plane-sync.md)
- [runtime-pull-loop.md](runtime-pull-loop.md)
- [apply-api.md](apply-api.md)

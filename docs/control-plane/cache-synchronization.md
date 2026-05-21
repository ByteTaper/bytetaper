# Cache Synchronization

## Overview

When policy changes, the Operational Plane must ensure **old cache entries are not reachable** under the new policy generation — even when disk-backed cleanup completes later.

Synchronization covers:

- Route cache **epoch** tags
- L1 memory **namespace** versioning
- L2 RocksDB **cleanup queue**
- **Materialized field variants**

## Route epoch bump

Each affected route receives a new cache epoch before the new snapshot is swapped. Lookups include epoch in the cache key namespace so entries written under generation *G* are invisible after activation to generation *G+1*.

**Immediate serving safety** depends on epoch isolation, not on L2 delete completion.

Component: `route_epoch_sync` / `RouteCacheEpochStore`.

## L1 cache namespace

L1 slots tied to changed routes are invalidated or reparented to the new namespace version via `cache_namespace_sync` and `tq_cache_namespace_versioning`.

Field-filter reductions require epoch bump **before** swap — see [activation-barrier.md](activation-barrier.md).

## L2 cleanup (async)

L2 entries for removed or changed routes are enqueued on `RouteCacheCleanupQueue`. Cleanup workers delete stale keys in the background.

| Property | Behavior |
|----------|----------|
| Async | Yes — does not block snapshot swap |
| Safe serving | Yes — epoch bump prevents stale hits |
| `cleanup_pending` | Fleet observability may report true until queue drains |

Component: `policy_cleanup_sync`.

## Materialized variants

Field variant indexes and materialized bodies for affected routes are invalidated via `materialized_variant_sync` so pagination/coalescing paths do not reuse incompatible variant sets.

## Persisted vs activated

| State | Cache behavior |
|-------|----------------|
| Committed only | Old snapshot + old epochs still serve |
| Activation in progress | Barrier preparing namespaces / epochs |
| Activated | New epoch + new snapshot; old entries unreachable |
| Cleanup pending | New policy serves; L2 delete may continue |

## Operational checklist

After apply, before declaring success:

1. `fleet.converged == true`
2. Per-runtime `activation_status` is `active` (not `activation_failed`)
3. No sustained `PolicyActivationFailed` in lifecycle logs
4. Optional: `cleanup_pending` cleared in fleet observability

## Tests

- `policy_activation_barrier_test.cpp` — epoch ordering
- Compose field-allowlist E2E — Envoy response fields match reduced allowlist

See [test-matrix.md](test-matrix.md).

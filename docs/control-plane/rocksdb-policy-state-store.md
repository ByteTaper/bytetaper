# RocksDBPolicyStateStore

## Overview

`RocksDBPolicyStateStore` is the initial production implementation of [`PolicyStateStore`](policy-state-store.md). It provides durable, single-process storage for committed policy versions, the active pointer, audit records, and apply job state.

Header: [`include/control_plane/rocksdb_policy_state_store.h`](../../include/control_plane/rocksdb_policy_state_store.h).

## Deployment path

Default Control Plane path (compose and server):

```text
/var/lib/bytetaper/policy-state-db
```

Environment: `BYTETAPER_POLICY_STATE_DB`.

## Critical limitations

```text
RocksDBPolicyStateStore is a single-writer Control Plane store.
It is NOT etcd.
It is NOT a consensus system.
It must NOT be shared by multiple pods as a raw RWX database.
```

| Constraint | Implication |
|------------|-------------|
| **Single writer** | Exactly one Control Plane process may open the DB for writes at a time |
| **Not HA** | Control Plane restart or host failure requires operational recovery (backup/restore) |
| **Not shared RWX** | Mounting the same directory read-write-shared across CP replicas will corrupt RocksDB |
| **Separate from L2** | Policy-state DB ≠ runtime L2 cache RocksDB — different paths, different lifecycle |

For multi-VM or multi-pod committed policy today: run **one** Control Plane instance with durable volume backup; runtimes pull over HTTP. Future HA requires a consensus-backed or external store backend (documented as extension path only).

## Why RocksDB (and not etcd) for v1

- Embedded, no external dependency for compose and single-node pilots
- Matches append-heavy audit + immutable version write pattern
- Abstract `PolicyStateStore` allows migration later

## Operations

| Operation | RocksDB behavior |
|-----------|------------------|
| Store version | Put immutable key for generation |
| Load version | Get by generation key |
| Active pointer | Dedicated key; CAS via expected-generation check |
| Audit | Append to audit key sequence |
| Jobs | Per-job keys for queue durability |

## Failure modes

| Symptom | Likely cause | Operator action |
|---------|--------------|-----------------|
| `open_error` on startup | Corrupt DB, permissions, path missing | Restore from backup; `down -v` only in dev |
| `PromoteActiveStale` | Concurrent apply CAS loss | Re-fetch active pointer; re-apply with correct CAS |
| `StoreSameVersionDifferentHashFails` | Version collision | Investigate duplicate generation write |
| Active pointer target missing | Version file deleted manually | Manual resolution — see [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md) |

## Backup and restore

1. Stop Control Plane process cleanly.
2. Snapshot `/var/lib/bytetaper/policy-state-db` (filesystem backup or volume snapshot).
3. On restore: ensure no second writer opens the same path; restart single CP instance.
4. Verify: `GET /admin/control-plane/policy/current` and `GET .../fleet/status`.

## Future extension path

When HA is required:

1. Implement `PolicyStateStore` for etcd, PostgreSQL, or object storage + metadata DB.
2. Run CP as stateless replicas talking to shared store with proper transactional semantics.
3. Keep runtime pull contract unchanged (`GET policy/current`, generation + canonical hash).

Do not attempt HA by sharing raw RocksDB files across nodes.

## Tests

See [test-matrix.md](test-matrix.md) §1 (PolicyStateStore regression).

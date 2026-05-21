# PolicyStateStore

## Overview

`PolicyStateStore` is the abstract persistence interface for committed Control Plane policy state. All global policy mutations flow through this store; runtime pods consume committed policy via pull but do not write to it.

Implementation: [`include/control_plane/policy_state_store.h`](../../include/control_plane/policy_state_store.h). Default backend: [`RocksDBPolicyStateStore`](rocksdb-policy-state-store.md).

## Responsibilities

| Operation | Purpose |
|-----------|---------|
| `store_policy_version` | Persist immutable canonical YAML for a generation |
| `load_policy_version` | Fetch version record + YAML by generation |
| `load_active_pointer` | Read current active committed pointer |
| `compare_and_promote_active` | CAS promote active pointer to a new generation |
| `append_audit_record` | Append operator/apply audit event |
| `commit_policy_generation_with_audit` | Atomic version store + pointer promote + audit |
| `store_policy_update_job` / `load_policy_update_job` | Persist async apply job state |

## Data model

### Active pointer

The **active pointer** is the authoritative “what generation is committed now” record:

- `generation`, `policy_id`, `canonical_hash`
- `source_type` (e.g. `taperql-apply`, `rollback`, `bootstrap-import`)
- `previous_generation` for lineage

Only one active pointer exists per `PolicyResourceKey` (default: `policy/default/runtime`).

### Immutable version records

Each successful mutation creates a new **immutable** version record. Versions are never updated in place. Rollback creates a **new** generation with older spec content — generation numbers always increase monotonically.

See [versioned-policy-history.md](versioned-policy-history.md).

### Audit records

Lifecycle and manual-resolution events are persisted for durability and operator forensics. See [observability.md](observability.md).

### Apply jobs

Async apply jobs are keyed by `job_id` and tracked through states: `Submitted` → `Queued` → `Processing` → `Committed` (or failure). See [apply-api.md](apply-api.md).

## Key layout

Keys are derived from `PolicyResourceKey` and entity type (`policy_state_key.h`):

- Active pointer per resource
- Version records per `(resource, generation)`
- Audit append log
- Job records per `(resource, job_id)`

## Compare-and-promote (CAS)

`compare_and_promote_active` requires an `ExpectedActivePolicy` (expected generation + policy id). Promotion fails if the store’s active pointer does not match — preventing lost updates when two operators apply concurrently.

Typical failure: `RejectedCasMismatch` / store `PromoteActiveStale`.

## Restart durability

The store must survive process restart:

- Active pointer and all version records remain readable after reopen
- Audit log append-only semantics preserved
- Compose and integration tests: `rocksdb_policy_state_store_test`, `control_plane_restart_durability_test` — see [test-matrix.md](test-matrix.md)

## Separation from L2 cache RocksDB

Policy state RocksDB (`BYTETAPER_POLICY_STATE_DB`) is a **different database instance** from the runtime L2 response-cache RocksDB. They must not share paths or column families. See [rocksdb-policy-state-store.md](rocksdb-policy-state-store.md).

## Non-goals

- Distributed consensus (not etcd, not multi-writer RWX)
- Runtime pods writing committed generations directly

## Future backends

The interface is designed so future backends (etcd, PostgreSQL, object storage) can replace RocksDB without changing runtime pull/activation logic. See [architecture-boundary.md](architecture-boundary.md) §12.

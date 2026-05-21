# Versioned Policy History

## Overview

Every successful Control Plane mutation creates a new **immutable** policy generation. Historical versions are retained for audit, rollback, and operator forensics. Generations only increase; rollback does not rewind the counter.

Related docs: [canonical-runtime-policy-yaml.md](canonical-runtime-policy-yaml.md), [persisted-policy-metadata.md](persisted-policy-metadata.md), [policy-state-store.md](policy-state-store.md).

## Layout (runtime mirror)

Runtime pods maintain a local last-known-good mirror under `BYTETAPER_POLICY_STATE_DIR` (compose: `/var/lib/bytetaper/policies`):

```text
/var/lib/bytetaper/policies/
  active-policy.yaml          # mirror of active committed spec (when synced)
  active-policy.meta.json   # metadata sidecar
  versions/
    0000000001-<policyId-prefix>.yaml
    0000000002-<policyId-prefix>.yaml
    ...
```

The Control Plane authoritative store is RocksDB (`policy-state-db`), not these files. The mirror enables offline LKG serving and mismatch detection.

## Immutability rules

| Rule | Description |
|------|-------------|
| Append-only generations | Generation `N` is written once; never updated in place |
| Monotonic counter | Each mutation increments generation by exactly 1 |
| Rollback = new generation | Restoring generation 2’s spec at generation 4 creates generation 4 with old content — not “go back to 2” |
| `policyId` / `canonicalHash` | Identity hashes change when spec changes; stored per version |

## Fetching a historical version

**HTTP (Control Plane):**

```bash
curl -fsS "http://localhost:19090/admin/control-plane/policy/version?resource_key=policy/default/runtime&generation=2" | jq .
```

Response includes `generation`, `policy_id`, `canonical_hash`, and `canonical_yaml`.

**Service API:** `ControlPlaneService::get_policy_version(key, generation)` and `list_policy_versions`.

## Active pointer vs history

- **Active pointer** — single current committed generation (what runtimes should converge to).
- **Version history** — all committed generations still retained in store.

If active pointer references a missing version file (mirror tamper or store corruption), startup validation reports `InactiveActivePointerMissingTarget` — no auto-repair. See [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md).

## Rollback semantics

Rollback via `ControlPlaneService::rollback`:

1. `plan_rollback` — preview target generation and `would_create_generation`.
2. `rollback` with `confirm=true` and CAS on current generation/policy id.
3. Store creates **new** generation containing target spec; `source_type` = `rollback`.

Rollback does **not** delete intermediate generations.

## Compose failure smoke: stale mirror tamper

The compose regression profile validates no-auto-recovery when an operator (or test) restores a **stale but self-consistent** older generation into the runtime mirror:

1. `GET /admin/control-plane/policy/version?generation=G-1` for canonical YAML and `versionedPolicyFile` path.
2. Restore matching `versions/…` file and metadata into runtime mirror.
3. Restart runtime — serves LKG stale generation briefly.
4. Fleet reports **stale/divergent** until pull reconverges to CP committed generation.
5. Control Plane must **not** promote stale mirror to a new committed generation automatically.

See [docker-compose-profile.md](docker-compose-profile.md) § Failure smoke and [test-matrix.md](test-matrix.md) §9.

## Bootstrap import

When the store has no active pointer, Control Plane imports bootstrap YAML once (`source.type: bootstrap-import`, generation 1). Bootstrap overwrite when active policy exists requires explicit confirmation — see [deployment-guardrails.md](deployment-guardrails.md).

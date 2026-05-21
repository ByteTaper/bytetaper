# Control Plane Manual Resolution Runbook

## When to use this runbook

Use when runtime local policy state does not match Control Plane committed state, or when activation/convergence fails.

> **ByteTaper does not auto-repair mismatched policy state.** Manual operator action is required.

## Decision tree

| Symptom | Likely classification | First action |
|---------|----------------------|--------------|
| Local mirror corrupted / unparseable | `InactiveCorruptedLocal` | `repair_local` or restore LKG from backup |
| Local generation behind CP | `Stale` / `RUNTIME_STATUS_STALE` | Wait for pull; if stuck, `repair_local` |
| Local hash ≠ committed hash | `Divergent` / `RUNTIME_STATUS_POLICY_MISMATCH` | If CP is correct: `repair_local`; if local is correct: `adopt_local` |
| Active pointer references missing version | `InactiveActivePointerMissingTarget` | Restore store from backup or rollback to valid generation |
| Bootstrap file ≠ committed | Bootstrap drift | Do not overwrite without confirm; re-apply intended policy |
| `activation_failed` on runtime | `Failed` | Check activation logs; fix policy; `repair_local` or rollback |
| Fleet not converged after apply | `Stale` / `Unreachable` | Check CP reachable, pull enabled, poll fleet status |

API reference: [manual-resolution-api.md](../../control-plane/manual-resolution-api.md).

## Prerequisites

1. Confirm committed state: `GET /admin/control-plane/policy/current`
2. Confirm fleet view: `GET /admin/control-plane/fleet/status`
3. Identify affected `runtime_id` from fleet `runtimes[]`
4. Record `generation`, `policy_id`, `canonical_hash` for CAS operations

```bash
CP=http://localhost:19090
curl -fsS "$CP/admin/control-plane/policy/current" | jq .
curl -fsS "$CP/admin/control-plane/fleet/status" | jq .
```

## Procedure: repair-local (CP authoritative)

Use when **committed** policy is correct and runtime mirror is stale, divergent, or corrupted.

1. **Plan** (service API / operator tool):
   - `plan_repair_local` with `runtime_id`, `expected_committed_generation`, `expected_committed_policy_id`
   - Verify plan shows correct committed hash
2. **Execute**:
   - `repair_local` with `confirm=true`, same expected committed fields
3. **Restart runtime** if activation does not recover automatically
4. **Verify**:
   ```bash
   curl -fsS "$CP/admin/control-plane/fleet/status" | jq '.fleet.converged, .runtimes'
   ```

Errors: `REPAIR_TARGET_RUNTIME_NOT_FOUND`, `REPAIR_COMMITTED_VERSION_MISMATCH`, `MANUAL_CONFIRMATION_REQUIRED`.

## Procedure: adopt-local (runtime authoritative)

Use only when operator has verified **local** policy is the intended source of truth and committed state is wrong.

1. **Plan**: `plan_adopt_local` — review `would_create_generation`, `candidate_policy_id`, `route_count`
2. **Execute**: `adopt_local` with `confirm_divergent_adoption=true` and CAS on `expected_current_generation`
3. **Verify** fleet converged and audit records show `ManualAdoptCompleted`

Errors: `ADOPT_LOCAL_POLICY_UNAVAILABLE`, `ADOPT_LOCAL_CONFIRMATION_REQUIRED`.

## Procedure: rollback

Use to restore spec from a **historical** generation as a new committed generation.

1. Fetch target: `GET /admin/control-plane/policy/version?generation=N`
2. **Plan**: `plan_rollback` with `target_generation=N`
3. **Execute**: `rollback` with:
   - `confirm=true`
   - `expected_current_generation` / `expected_current_policy_id` from current active pointer
4. Wait for fleet convergence
5. Confirm new generation = old spec, monotonic generation counter

Errors: `ROLLBACK_TARGET_NOT_FOUND`, `ROLLBACK_EXPECTED_CURRENT_MISMATCH`.

## Procedure: re-apply intended policy

When committed state should change to new YAML (not historical rollback):

1. `GET /admin/control-plane/policy/current` → CAS fields
2. `POST /admin/control-plane/policy/apply` with updated `source` and `expected_base_*`
3. Poll fleet until `converged`

See [apply-api.md](../../control-plane/apply-api.md).

## Procedure: stale mirror tamper recovery

If a stale generation was restored into the runtime mirror (accident or test):

1. **Do not** expect CP to auto-promote stale mirror
2. Ensure CP committed generation is correct (`policy/current`)
3. Restart runtime or wait for pull loop
4. Fleet should move from `Stale`/`Divergent` → `Converged`
5. If stuck: `repair_local`

Reference: [versioned-policy-history.md](../../control-plane/versioned-policy-history.md).

## Procedure: Control Plane store recovery

If `policy-state-db` is corrupt or active pointer target missing:

1. Stop Control Plane (single writer)
2. Restore RocksDB from backup ([rocksdb-policy-state-store.md](../../control-plane/rocksdb-policy-state-store.md))
3. Start CP; verify `policy/current`
4. Run `repair_local` on all runtimes if needed

## Verification checklist

- [ ] `policy/current` shows expected generation and hash
- [ ] All runtimes `convergence_status: Converged`
- [ ] `fleet.converged: true`
- [ ] Data path smoke: Envoy route returns expected behavior
- [ ] No `PolicyMismatchDetected` events in recent audit/lifecycle logs

Compose regression: `make test-control-plane-compose`

## Escalation

| Issue | Escalation |
|-------|------------|
| Repeated CAS failures | Coordinate single operator; investigate concurrent applies |
| Activation failures | Collect `PolicyActivationFailed` logs with `stage` and `error_code` |
| Store promote conflicts | `MANUAL_OPERATION_PROMOTE_CONFLICT` — verify no split-brain CP writers |

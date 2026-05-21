# Manual Resolution API

## Overview

When runtime local policy state does not match Control Plane committed state, ByteTaper **does not auto-repair**. Operators must use explicit manual resolution operations.

These operations are implemented on **`ControlPlaneService`** (in-process API). They are **not** exposed on Control Plane HTTP today. Use integration tests, operator tooling, or future HTTP routes.

Operator procedures: [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md).

## Principles

| Principle | Description |
|-----------|-------------|
| **No auto-repair** | Stale, diverged, or corrupted local mirrors are not silently fixed |
| **Plan then confirm** | Destructive ops support `plan_*` preview before `confirm=true` execute |
| **CAS on commit** | Rollback and adopt require expected current generation/policy id |
| **Audit** | All manual ops emit lifecycle events and audit records |

## repair-local

Push committed Control Plane policy to a runtime whose local mirror is wrong but CP state is authoritative.

### `plan_repair_local`

Request: `PolicyRepairLocalPlanRequest`

| Field | Description |
|-------|-------------|
| `runtime_id` | Target runtime |
| `expected_committed_generation` | CP generation operator believes is committed |
| `expected_committed_policy_id` | CP policy id |
| `operator_id`, `request_id` | Audit |

Success: `RepairLocalPlanReady` with committed generation/hash preview.

### `repair_local`

Request: `PolicyRepairLocalRequest` — same fields plus `confirm=true`.

Success: `RepairLocalAccepted`.

| Error code | Meaning |
|------------|---------|
| `REPAIR_TARGET_RUNTIME_NOT_FOUND` | Unknown runtime id |
| `REPAIR_COMMITTED_VERSION_MISMATCH` | Expected committed does not match store |
| `MANUAL_CONFIRMATION_REQUIRED` | `confirm` not set |

## adopt-local

Promote a runtime’s local divergent policy to a **new** committed generation (operator asserts local is correct).

### `plan_adopt_local`

Returns `AdoptLocalPlanReady` with `would_create_generation`, `candidate_policy_id`, `route_count`.

### `adopt_local`

Requires `confirm_divergent_adoption=true` when adopting divergent local state.

| Error code | Meaning |
|------------|---------|
| `ADOPT_LOCAL_POLICY_UNAVAILABLE` | Cannot fetch local policy |
| `ADOPT_LOCAL_POLICY_INVALID` | Local policy fails validation |
| `ADOPT_LOCAL_CONFIRMATION_REQUIRED` | Confirmation flag missing |

## rollback

Restore spec from a historical generation as a **new** committed generation.

### `plan_rollback`

Request: `PolicyRollbackPlanRequest` with `target_generation`.

Returns `RollbackPlanReady` with `would_create_generation`.

### `rollback`

Request: `PolicyRollbackRequest`

| Field | Description |
|-------|-------------|
| `target_generation` | Historical generation to restore spec from |
| `expected_current_generation` | CAS |
| `expected_current_policy_id` | CAS |
| `confirm` | Must be `true` |

Success: `RollbackAccepted`; new `source_type` = `rollback`.

| Error code | Meaning |
|------------|---------|
| `ROLLBACK_TARGET_NOT_FOUND` | Generation not in store |
| `ROLLBACK_EXPECTED_CURRENT_MISMATCH` | CAS failure |
| `ROLLBACK_CONFIRMATION_REQUIRED` | `confirm` not set |

## Common manual error codes

| Code | Meaning |
|------|---------|
| `MANUAL_INVALID_REQUEST` | Validation failure |
| `MANUAL_POLICY_STATE_UNAVAILABLE` | Store unavailable |
| `MANUAL_OPERATION_PROMOTE_CONFLICT` | CAS promote failed |
| `MANUAL_OPERATION_ACTIVATION_FAILED` | Post-commit activation failed |
| `MANUAL_AUDIT_WRITE_FAILED` | Audit append failed |

Constants: [`manual_resolution_api.h`](../../include/control_plane/manual_resolution_api.h).

## PolicyApplyStatus values

| Status | Operation |
|--------|-----------|
| `RepairLocalPlanReady` | Plan repair |
| `RepairLocalAccepted` | Execute repair |
| `AdoptLocalPlanReady` | Plan adopt |
| `AdoptLocalAccepted` | Execute adopt |
| `RollbackPlanReady` | Plan rollback |
| `RollbackAccepted` | Execute rollback |
| `RejectedConfirmationRequired` | Missing confirm flag |

## Re-apply intended policy

When committed state is correct but runtime is stale, prefer:

1. Verify `GET /admin/control-plane/policy/current`
2. `repair_local` or wait for pull loop to reconverge
3. `GET /admin/control-plane/fleet/status` until `Converged`

If committed state itself is wrong, use `rollback` or a normal `apply` with correct CAS.

## Tests

`manual_resolution_service_test.cpp` — plan/confirm flows, audit records, rollback CAS.

See [test-matrix.md](test-matrix.md).

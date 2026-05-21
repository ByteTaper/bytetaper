# Control Plane Apply API

## Overview

Policy apply submits an asynchronous job that validates, compiles, stores an immutable version, promotes the active pointer (CAS-protected), and triggers runtime pull/activation. The HTTP surface accepts YAML route source; the in-process API also supports TaperQuery IR source.

## HTTP: POST `/admin/control-plane/policy/apply`

### Request body

```json
{
  "source": "routes:\n  - id: small-json\n    ...",
  "request_id": "cp-demo-apply-1",
  "operator_id": "control-plane-demo",
  "expected_base_generation": 2,
  "expected_base_policy_id": "<from GET policy/current>"
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `source` | Yes | YAML route policy body (inline string) |
| `request_id` | Recommended | Idempotency / audit correlation |
| `operator_id` | Recommended | Operator identity for audit |
| `expected_base_generation` | Yes (CAS) | Generation from `GET /admin/control-plane/policy/current` |
| `expected_base_policy_id` | Yes (CAS) | `policy_id` from the same response |

Route YAML template: [`examples/taperquery/apply-policy-source.yaml`](../../examples/taperquery/apply-policy-source.yaml). Do not reuse fixed CAS values from [`apply-policy.json`](../../examples/taperquery/apply-policy.json) — bootstrap import changes generation and `policy_id` per run.

### Response

```json
{
  "ok": true,
  "status": "Accepted",
  "resource_key": "policy/default/runtime",
  "job_id": "apply-...",
  "message": "...",
  "error": ""
}
```

| `status` (success path) | Meaning |
|-------------------------|---------|
| `Accepted` | Job enqueued |
| `Applied` | Synchronous completion (uncommon on HTTP) |

Failure returns `ok: false` with `status` such as `RejectedCasMismatch`, `RejectedQueueFull`, `RejectedCompileError`, etc.

### Example

Build the request body from the active pointer, then apply (same pattern as [`_control_plane_compose_lib.sh`](../../scripts/test/_control_plane_compose_lib.sh) `cp_apply_policy_yaml`):

```bash
CP=http://localhost:19090
YAML=examples/taperquery/apply-policy-source.yaml

CURRENT="$(curl -fsS "${CP}/admin/control-plane/policy/current")"
GEN="$(echo "${CURRENT}" | jq -r '.generation')"
PID="$(echo "${CURRENT}" | jq -r '.policy_id')"

jq -n \
  --rawfile source "${YAML}" \
  --arg request_id "cp-demo-apply-1" \
  --arg operator_id "control-plane-demo" \
  --argjson expected_base_generation "${GEN}" \
  --arg expected_base_policy_id "${PID}" \
  '{source: $source, request_id: $request_id, operator_id: $operator_id,
    expected_base_generation: $expected_base_generation,
    expected_base_policy_id: $expected_base_policy_id}' \
  | curl -fsS -X POST "${CP}/admin/control-plane/policy/apply" \
      -H 'content-type: application/json' -d @- | jq .
```

## HTTP: GET `/admin/control-plane/policy/current`

### Response (success)

```json
{
  "ok": true,
  "resource_key": "policy/default/runtime",
  "generation": 2,
  "policy_id": "abc123...",
  "canonical_hash": "sha256:...",
  "schema_version": 1,
  "api_version": "bytetaper.io/v1alpha1",
  "kind": "RuntimePolicy",
  "source_type": "taperql-apply",
  "previous_generation": 1
}
```

## Apply job lifecycle

Async jobs progress through queue states (per shard FIFO per `resource_key`):

```text
Submitted → Queued → Processing → Committed
```

Query job state (service API only today):

```cpp
get_policy_update_job(job_id, resource_key)
```

Integration test reference: `ApplyJobObservesSubmittedQueuedProcessingCommitted` in `control_plane_service_contract_test.cpp`.

## In-process API

### `dry_run`

Validates and compiles candidate policy without storing. Returns `DryRunReady` with `PolicyRouteDiffPlan` and diagnostics.

### `diff`

Computes route diff between base and candidate. Returns `DiffReady`.

### `apply`

Same CAS fields as HTTP. `PolicyApplySourceType`: `Yaml` or `TaperQuery`. Flags:

- `strict_production` — production validation rules
- `include_unchanged_routes` / `include_field_level_changes` — diff granularity

### Rollback (service API)

- `plan_rollback` — `RollbackPlanReady` with `would_create_generation`
- `rollback` — requires `confirm=true` and CAS on current generation

See [manual-resolution-api.md](manual-resolution-api.md).

## Common rejection statuses

| Status | Typical cause |
|--------|----------------|
| `RejectedCasMismatch` | `expected_base_*` does not match store active pointer |
| `RejectedQueueFull` | Policy update queue at capacity |
| `RejectedCompileError` | Invalid routes / policy IR |
| `RejectedValidation` | Schema or production guardrail failure |
| `RejectedParseError` | Malformed YAML/JSON source |
| `RejectedNoChanges` | Candidate identical to base (strict mode) |
| `RejectedStorageUnavailable` | Store not open or I/O error |
| `RejectedConfirmationRequired` | Dangerous op without `confirm` |

## Activation statuses (post-commit)

After commit, runtime activation may report:

| Status | Meaning |
|--------|---------|
| `PolicyCommittedNotActivated` | Stored but not yet activated on runtime |
| `PolicyActivationInProgress` | Barrier running |
| `PolicyActivationFailed` | Barrier failed; old snapshot may still serve |
| `PolicyActiveCleanupPending` | Activated; async cleanup pending |
| `PolicyActive` | Fully activated |

See [activation-barrier.md](activation-barrier.md).

## CAS workflow

1. `GET /admin/control-plane/policy/current?resource_key=policy/default/runtime` → `generation`, `policy_id`
2. Build apply JSON with `source` from [`apply-policy-source.yaml`](../../examples/taperquery/apply-policy-source.yaml) and those fields as `expected_base_*`
3. `POST /admin/control-plane/policy/apply`
4. On `RejectedCasMismatch`, refresh pointer and retry or abort
5. `GET /admin/control-plane/fleet/status?resource_key=policy/default/runtime` until `fleet.converged` is true

See [fleet-status-api.md](fleet-status-api.md) and [runtime-pull-loop.md](runtime-pull-loop.md).

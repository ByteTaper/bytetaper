# TaperQL HTTP Operations Runbook

## Purpose & Audience

This runbook is designed for ByteTaper operators, developers, and Site Reliability Engineers (SREs). It provides standard operating procedures (SOPs), copy-pasteable commands, and troubleshooting guides for inspecting, dry-running, applying, and recovering from TaperQL policy updates via the local Docker Compose development stack and production HTTP control planes.

---

## Prerequisites

Before executing these operations, ensure the following core tools are installed on your workstation. Run the quick-check commands to verify version compatibility:

```bash
docker compose version
curl --version
jq --version
```

---

## Endpoint Reference

The ByteTaper container runtime binds three distinct network ports for isolation:

```
18080  extproc gRPC
18081  metrics / healthz / readyz
18082  TaperQL admin HTTP
```

### Complete URL Reference
```
GET  http://localhost:18081/readyz
GET  http://localhost:18081/healthz
GET  http://localhost:18081/metrics
GET  http://localhost:18082/admin/taperquery/policy/current
POST http://localhost:18082/admin/taperquery/apply
GET  http://localhost:18082/admin/taperquery/audit/latest
```

---

## Start Local Stack

Launch the ByteTaper external processor service within your local development environment:

```bash
# Start the service in the background
docker compose up -d bytetaper-extproc

# Verify running container processes
docker compose ps bytetaper-extproc

# Follow startup logs to verify successful policy compilation
docker compose logs -f bytetaper-extproc

# Verify service readiness via TCP healthcheck endpoint
curl -i http://localhost:18081/readyz

# Inspect the active runtime policy
curl -s http://localhost:18082/admin/taperquery/policy/current | jq .
```

---

## Disable Admin Endpoint

To completely disable the TaperQL Admin HTTP mutating interface (e.g., in hardened production environments):

```bash
export BYTETAPER_ADMIN_ENABLED=0
docker compose up -d bytetaper-extproc
```

**Expected Behavior:** When disabled, the container process will not listen on port `18082`, and all administrative requests will fail immediately with `Connection refused`.

---

## Security Warning

> [!WARNING]
> The TaperQL Admin HTTP interface is an **unauthenticated, mutating API**. It allows direct modification of routing, caching, and coalescing behavior in real time. It must **never** be exposed to public networks or untrusted traffic. For production deployments, bind strictly to loopback (`127.0.0.1`) or secure within a dedicated administrative service mesh ingress.

---

## SOP: Current Policy

Inspect the currently active runtime policy snapshot and its cryptographic identity SHA.

```bash
curl -s http://localhost:18082/admin/taperquery/policy/current | jq .
```

### Field Definitions & Response Structure
```json
{
  "ok": true,
  "policy_identity": "a1b2c3d4e5f6...",
  "generation": 1,
  "route_count": 5,
  "source_name": "examples/policy/bytetaper-policy.yaml",
  "version": {
    "source_schema_version": "yaml/v1",
    "policy_ir_version": "tq-ir/v1",
    "identity_version": "policy-identity/v2",
    "emitter_version": "v1",
    "runtime_min_version": "v1",
    "runtime_capability_profile": "baseline"
  },
  "has_latest_apply": false,
  "latest_apply": null
}
```

*(Note: Upon initial startup, `has_latest_apply` is `false` and `latest_apply` is `null`. After a successful policy mutation via `POST /apply`, `latest_apply` becomes populated with the compact latest-apply summary.)*

> [!CAUTION]
> If `policy_identity` is `null` or empty, **STOP**. This indicates that no active runtime policy snapshot is installed. Investigate startup logs immediately for policy file parsing or IR compilation errors.

---

## SOP: Dry-Run

The Dry-Run workflow validates syntax, Intermediate Representation (IR) compilation, and semantic constraints without altering the active serving snapshot.

```bash
# 1. Fetch current policy identity
CURRENT_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)
if [ -z "$CURRENT_SHA" ] || [ "$CURRENT_SHA" = "null" ]; then echo "FATAL: active policy identity is null"; exit 1; fi

# 2. Build dry-run candidate payload
cat <<EOF > /tmp/taperql-dry-run.json
{
  "source_type": "taperquery",
  "source": "policy \"my-policy\" { route \"r1\" when path prefix \"/api\" {} }",
  "expected_base_identity": "$CURRENT_SHA",
  "mode": "dry_run"
}
EOF

# 3. Submit dry-run request
curl -s -X POST http://localhost:18082/admin/taperquery/apply \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/taperql-dry-run.json | jq .

# Expect response containing: "status": "DryRunReady"

# 4. Verify active policy identity remained perfectly unchanged
CHECK_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)
test "$CURRENT_SHA" = "$CHECK_SHA" && echo "PASS: dry-run did not mutate policy"
```

---

## SOP: Apply

The Apply workflow atomically commits a new policy candidate to active serving traffic.

```bash
# 1. Fetch current policy identity
CURRENT_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)

# 2. Build apply candidate payload
cat <<EOF > /tmp/taperql-apply.json
{
  "source_type": "taperquery",
  "source": "policy \"my-policy\" { route \"prod_route\" when path prefix \"/api\" {} }",
  "expected_base_identity": "$CURRENT_SHA",
  "operator_id": "sre-oncall",
  "request_id": "change-ticket-8921",
  "mode": "apply"
}
EOF

# 3. Submit apply request and capture response
curl -s -X POST http://localhost:18082/admin/taperquery/apply \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/taperql-apply.json | tee /tmp/taperql-apply-response.json | jq .

# Expect response containing: "ok": true, "status": "Applied"

# 4. Verify new policy identity and incremented generation
curl -s http://localhost:18082/admin/taperquery/policy/current | jq .

# 5. Optional Envoy smoke test against local stack
curl -i http://localhost:10000/api/v1/products
```

---

## CAS Conflict Workflow

A `409 Conflict` (`CAS_MISMATCH`) occurs when another operator or automated process successfully applies a policy update between your initial inspection and candidate submission.

### Example Rejection Response
```json
{
  "ok": false,
  "status": "RejectedCasMismatch",
  "error_code": "CAS_MISMATCH",
  "message": "Compare-and-swap mismatch. Current active identity does not match expected base identity.",
  "expected_base_identity": "stale-sha",
  "current_policy_identity": "actual-live-sha"
}
```

### Remediation & Retry Steps
```bash
# 1. Fetch the latest live policy SHA
LATEST_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)

# 2. Re-inspect the active policy metadata to understand recent changes (note: full source string is not returned)
curl -s http://localhost:18082/admin/taperquery/policy/current | jq '{policy_identity,generation,route_count,source_name,version,latest_apply}'

# 3. Re-build payload with new expected base identity
jq --arg sha "$LATEST_SHA" '.expected_base_identity = $sha' /tmp/taperql-apply.json > /tmp/taperql-apply-retry.json

# 4. Re-submit apply request
curl -s -X POST http://localhost:18082/admin/taperquery/apply \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/taperql-apply-retry.json | jq .
```

---

## No-Op Apply Workflow

When an operator submits a candidate policy that compiles to an identical AST/IR as the currently active policy, the runtime rejects the mutation to avoid unnecessary pointer swaps.

```json
{
  "ok": false,
  "status": "RejectedNoChanges",
  "error_code": "NO_CHANGES",
  "message": "Candidate policy produces identical AST/IR. No changes required."
}
```

**Operator Action:** No remediation required. The active serving policy already matches your desired end state.

---

## Error Response Guide

| HTTP Code | Error Code | Common Cause | Operator Action |
|---|---|---|---|
| `400 Bad Request` | `INVALID_REQUEST` | Malformed JSON, missing required fields, or unsupported `source_type`. | Validate JSON payload syntax and ensure `expected_base_identity` and `mode` are populated. |
| `409 Conflict` | `CAS_MISMATCH` | Concurrent modification by another operator. | Follow CAS Conflict Workflow to rebase against latest SHA. |
| `413 Too Large` | `PAYLOAD_TOO_LARGE` | Candidate body exceeds configured byte limit (default 4MB). | Split massive policy configurations or remove excessive whitespace/comments. |
| `422 Unprocessable`| `VALIDATION_ERROR` | Syntax error, unknown keyword, or semantic conflict (e.g., duplicate route ID). | Inspect the `issues` and `diagnostics` arrays in the response for line-level error hints. |
| `500 Internal` | `SNAPSHOT_ERROR` | Internal failure during snapshot memory allocation or IR lowering. | Follow Incident Runbook to capture state and escalate to maintainers. |
| `503 Unavailable` | `NO_ACTIVE_POLICY` | Policy store uninitialized or runtime shutting down. | Verify service health via `/readyz` and inspect startup logs. |

---

## Active Policy Persistence & Recovery

To ensure that dynamically applied TaperQL policies are preserved across process, container, or VM restarts, ByteTaper supports atomic, active policy persistence.

### Configuration

Persistence can be enabled and configured using either environment variables or command-line arguments:

#### Environment Variables
- `BYTETAPER_POLICY_PERSISTENCE_ENABLED=1`: Enables policy persistence (defaults to `1` in Docker Compose).
- `BYTETAPER_POLICY_STATE_DIR="/path/to/state"`: Directory where `active-policy.yaml` and `active-policy.meta.json` will be stored.

#### CLI Arguments
- `--policy-state-dir <path>`: Specifies the state directory (takes precedence over environment variables).
- `--disable-policy-persistence`: Explicitly disables persistence entirely (takes highest precedence).

### Recovery & Startup Behaviors

When the ByteTaper server starts up, it automatically performs the following recovery check:
1. **Graceful Fallback**: If no persisted active policy or metadata files are found, the server starts up cleanly using the default bootstrap policy file (`--policy-file`).
2. **Strict Integrity Check**: The persisted policy is validated using SHA-256 digest validation. If the files are corrupt, incomplete, or the computed hash does not match the value stored in the metadata JSON file, the server **rejects the load, prints a fatal error, and aborts startup immediately (returning exit code 1)**. This prevents serving stale or undefined routing/caching rules.

### Local-Only Multi-Pod/VM Limitation

> [!IMPORTANT]
> **Active policy persistence is strictly local to the host filesystem.**
>
> In load-balanced, multi-instance settings (such as a Kubernetes Deployment with multiple pods or a VM scale set):
> - A `POST /admin/taperquery/apply` request only applies and persists the policy to the **specific instance** that handled the HTTP request.
> - Other instances will continue serving their previous policy state, leading to inconsistent behavior.
>
> **Recommended Production Patterns**:
> 1. **Centralized Dispatch**: Use an external deployer, CI/CD pipeline, or admin control plane to dispatch the `POST /admin/taperquery/apply` request concurrently to **all** active instances.
> 2. **Shared Volume Mount**: Mount a shared network storage volume (e.g., ReadWriteMany NFS) to the configured state directory so all instances read the same persisted policy on startup or restart.

---

## Troubleshooting Commands

```bash
# Verify TCP readiness
curl -i http://localhost:18081/readyz

# Verify internal health state
curl -i http://localhost:18081/healthz

# Inspect Prometheus runtime metrics
curl -s http://localhost:18081/metrics | grep bytetaper_

# Inspect current policy and audit summary
curl -s http://localhost:18082/admin/taperquery/policy/current | jq .

# Follow real-time extproc logs
docker compose logs --tail=100 -f bytetaper-extproc

# Restart extproc service
docker compose restart bytetaper-extproc

# Rebuild local development binary
docker compose run --rm bytetaper-build-server

# Run Admin HTTP integration test suite
docker compose run --rm bytetaper-admin-http-test
```

---

## Pre-Apply Safety Checklist

### Before Apply
- `[ ]` 1. Active policy identity fetched immediately prior to payload construction.
- `[ ]` 2. Candidate syntax successfully validated via Dry-Run mode (`status: DryRunReady`).
- `[ ]` 3. No blocker or high-severity warnings present in the dry-run `issues` array.
- `[ ]` 4. Compare-And-Swap (CAS) `expected_base_identity` explicitly populated.
- `[ ]` 5. Change management ticket ID populated in `request_id`.
- `[ ]` 6. Operator identifier populated in `operator_id`.
- `[ ]` 7. Route shadowing analysis reviewed in dry-run output (`may_shadow_later_routes`).
- `[ ]` 8. Rollback candidate source code accessible and ready.

### After Apply
- `[ ]` 1. HTTP 200 OK and `status: Applied` confirmed in apply response.
- `[ ]` 2. Active policy identity matches `applied_policy_identity`.
- `[ ]` 3. Generation counter incremented exactly by 1.
- `[ ]` 4. Envoy extproc gRPC active streams remain stable without disconnects.
- `[ ]` 5. Downstream smoke tests passing (e.g., HTTP 200 on key API routes).
- `[ ]` 6. Audit store reflects correct outcome and route modification counts.

---

## Incident Runbook

### Scenario A: Dry-Run Rejection
1. Capture the `issues` and `diagnostics` arrays from the JSON response.
2. Locate the specific `route_id` and `field_path` cited in the error hint.
3. Correct the syntax or semantic violation in your candidate source string and re-test.

### Scenario B: Compare-And-Swap (CAS) Mismatch
1. Execute `CURRENT_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)`.
2. Compare active metadata (`source_name`, `latest_apply`, `/admin/taperquery/audit/latest`), change management tickets, deployment artifacts, or repository source code to identify what changed (note: the admin endpoint does not return the full active policy source string).
3. Update `expected_base_identity` with `$CURRENT_SHA` and re-submit.

### Scenario C: HTTP 500 Internal Server Error
If the admin server returns a 500 error during snapshot generation, capture debug state immediately before restarting:

```bash
curl -s http://localhost:18082/admin/taperquery/policy/current | jq . > /tmp/current-policy-debug.json
docker compose logs --tail=500 bytetaper-extproc > /tmp/bytetaper-extproc-debug.log
```
Escalate captured logs and debug JSON to core ByteTaper maintainers.

### Scenario D: Applied Policy Breaks Production Traffic
If an applied policy introduces unexpected latency or downstream errors, execute an immediate rollback using the previous known-good policy:

```bash
# 1. Fetch currently active (bad) identity
BAD_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)

# 2. Submit previous known-good policy candidate
cat <<EOF > /tmp/taperql-rollback.json
{
  "source_type": "taperquery",
  "source": "<previous_known_good_policy_source_string>",
  "expected_base_identity": "$BAD_SHA",
  "operator_id": "sre-oncall",
  "request_id": "INC-10293-rollback",
  "mode": "apply"
}
EOF

curl -s -X POST http://localhost:18082/admin/taperquery/apply \
  -H "Content-Type: application/json" \
  --data-binary @/tmp/taperql-rollback.json | jq .
```

---

## Audit Inspection

Inspect recent configuration changes and operator audit trails directly from the current policy summary or dedicated audit endpoint.

```bash
# Extract compact latest apply audit trace
curl -s http://localhost:18082/admin/taperquery/policy/current | jq .latest_apply

# Fetch full comprehensive audit record from dedicated endpoint
curl -s http://localhost:18082/admin/taperquery/audit/latest | jq .
```

---

## Reusable Shell Helpers

Add these functions to your `~/.bashrc` or `~/.zshrc` for streamlined TaperQL CLI administration:

```bash
export ADMIN_URL="${ADMIN_URL:-http://localhost:18082}"

current_policy() {
  curl -s "$ADMIN_URL/admin/taperquery/policy/current" | jq .
}

current_sha() {
  curl -s "$ADMIN_URL/admin/taperquery/policy/current" | jq -r .policy_identity
}

apply_file() {
  if [ -z "$1" ]; then echo "usage: apply_file <payload.json>"; return 1; fi
  curl -s -X POST "$ADMIN_URL/admin/taperquery/apply" \
    -H "Content-Type: application/json" \
    --data-binary "@$1" | jq .
}
```

### Example Helper Usage
```bash
# Fetch active SHA
SHA=$(current_sha)

# Build payload and apply
cat <<EOF > /tmp/payload.json
{
  "source_type": "taperquery",
  "source": "policy \"app\" { route \"r1\" when path prefix \"/\" {} }",
  "expected_base_identity": "$SHA",
  "mode": "apply"
}
EOF

apply_file /tmp/payload.json
```

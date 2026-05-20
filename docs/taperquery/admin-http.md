# TaperQuery Admin HTTP Guide

The TaperQuery Admin HTTP server provides a mutating control-plane API for dynamically inspecting, dry-running, and applying runtime policy updates without process restarts.

## Purpose & Architecture

This server is strictly a **runtime-local** administrative interface. It coordinates with the internal `RuntimePolicyStore` and `TqApplyService` to validate and atomically swap active compiled policy snapshots on a single process.

For multi-service Control Plane + Runtime deployments, committed policy mutations go through the dedicated Control Plane HTTP API on port **19090** (`/admin/control-plane/*`). See [docker-compose-profile.md](../control-plane/docker-compose-profile.md).

> [!WARNING]
> This interface is **unauthenticated** and allows arbitrary policy mutation. It must **never** be exposed to public networks or untrusted traffic.

---

## Local Docker Startup

In the local development workflow, the Admin HTTP server is exposed on port `18082`.

```bash
# Start the ExtProc service in the background
docker compose up -d bytetaper-extproc

# Verify readiness via the metrics/readyz port
curl -i http://localhost:18081/readyz

# Inspect the active runtime policy
curl -s http://localhost:18082/admin/taperquery/policy/current | jq .
```

---

## Endpoint Reference

### 1. Inspect Active Policy (`GET /admin/taperquery/policy/current`)

Returns the currently loaded runtime policy along with its active identity SHA and generation counter.

```bash
curl -s http://localhost:18082/admin/taperquery/policy/current | jq .
```

**Example Response:**
```json
{
  "ok": true,
  "policy_identity": "a1b2c3d4e5f6...",
  "generation": 1,
  "route_count": 5,
  "source_name": "examples/policy/bytetaper-policy.yaml",
  "version": {
    "source_schema_version": "v1alpha1",
    "policy_ir_version": "v1",
    "identity_version": "v1",
    "emitter_version": "v1",
    "runtime_min_version": "v1",
    "runtime_capability_profile": "baseline"
  },
  "has_latest_apply": true,
  "latest_apply": {
    "sequence": 1,
    "unix_epoch_ms": 1715640000000,
    "outcome": "Applied",
    "request_id": "req-123",
    "operator_id": "operator-alice",
    "mode": "apply",
    "status": "Applied",
    "message": "Policy successfully applied.",
    "previous_policy_identity": "old-sha...",
    "expected_base_identity": "old-sha...",
    "candidate_policy_identity": "a1b2c3d4e5f6...",
    "applied_policy_identity": "a1b2c3d4e5f6...",
    "before_generation": 0,
    "after_generation": 1,
    "added_routes": 5,
    "removed_routes": 0,
    "modified_routes": 0,
    "unchanged_routes": 0,
    "issue_count": 0
  }
}
```

---

### 2. Apply / Dry-Run Policy (`POST /admin/taperquery/apply`)

Submits a new policy candidate. Requires strict Compare-And-Swap (CAS) concurrency control via `expected_base_identity`.

```json
{
  "source_type": "taperquery",
  "source": "policy \"my-policy\" { route \"new_route\" when path prefix \"/api\" {} }",
  "expected_base_identity": "<current_policy_identity>",
  "mode": "dry_run" // or "apply"
}
```

---

## Workflows

### Dry-Run Workflow

Validates syntax, compilation, and semantic constraints without altering the active serving snapshot.

```bash
# 1. Fetch current policy identity
CURRENT_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)

# 2. Submit dry-run candidate
curl -s -X POST http://localhost:18082/admin/taperquery/apply \
  -H "Content-Type: application/json" \
  -d "{\"source_type\":\"taperquery\",\"source\":\"policy \\\"test\\\" { route \\\"r1\\\" when path prefix \\\"/api\\\" {} }\",\"expected_base_identity\":\"${CURRENT_SHA}\",\"mode\":\"dry_run\"}" | jq .
```

**Response:**
```json
{
  "ok": true,
  "status": "DryRunReady",
  "applied_policy_identity": "f8e7d6c5b4...",
  "after_generation": 1
}
```

---

### Compare-And-Swap (CAS) Apply Workflow

Atomically replaces the serving snapshot. Fails immediately with `409 Conflict` if another operator or process committed an update in the interim.

```bash
# 1. Fetch current base identity
CURRENT_SHA=$(curl -s http://localhost:18082/admin/taperquery/policy/current | jq -r .policy_identity)

# 2. Submit real apply commit
curl -s -X POST http://localhost:18082/admin/taperquery/apply \
  -H "Content-Type: application/json" \
  -d "{\"source_type\":\"taperquery\",\"source\":\"policy \\\"prod\\\" { route \\\"r1\\\" when path prefix \\\"/api\\\" {} }\",\"expected_base_identity\":\"${CURRENT_SHA}\",\"mode\":\"apply\"}" | jq .
```

**Response:**
```json
{
  "ok": true,
  "status": "Applied",
  "applied_policy_identity": "f8e7d6c5b4...",
  "after_generation": 2
}
```

---

### CAS Mismatch Handling

If an operator submits a stale candidate:

```json
{
  "ok": false,
  "status": "RejectedCasMismatch",
  "error_code": "CAS_MISMATCH",
  "error_message": "Compare-and-swap mismatch. Current active identity does not match expected base identity.",
  "expected_base_identity": "stale-sha",
  "current_policy_identity": "actual-live-sha"
}
```

**Remediation:**
1. Fetch the latest policy using `GET /admin/taperquery/policy/current`.
2. Inspect the changes to ensure no conflicting intent.
3. Re-submit the candidate passing the new `current_policy_identity`.

---

### 3. Applying Invalidation Policies

The `PATCH /admin/taperquery/apply` (or POST in the current API) endpoint supports routes with `cache.invalidation` blocks.

**Example Invalidation Payload:**
```json
{
  "source_type": "taperquery",
  "source": "policy \"patch-invalidation\" { route \"get_user\" when method GET path prefix \"/users/\" { cache { enabled true behavior store } } route \"update_user\" when method PATCH path prefix \"/users/\" { cache { invalidation { enabled true on_methods [\"PATCH\"] targets [ { route_id \"get_user\" strategy \"route_epoch\" } ] } } } }",
  "expected_base_identity": "<current_sha>",
  "mode": "apply"
}
```

**Validation Notes:**
- **Dry-Run Validation**: The TaperQuery service validates that all `targets` exist in the *new* candidate policy document (not just the old one) and that they are cacheable `GET` routes. In the example above, `get_user` must be present in the `source` string.
- **CAS Protection**: Applying an invalidation policy uses the same Compare-And-Swap protection to ensure you aren't invalidating routes that have been modified or removed by another operator.
- **Immediate Effect**: Once applied, the next matching mutation request will begin tracking invalidation plans and bumping epochs on success.

---

## HTTP Status Codes

| Status | Code | Description |
|---|---|---|
| `200 OK` | Success | Policy inspected, dry-run ready, or successfully applied. |
| `400 Bad Request` | `INVALID_REQUEST` | Malformed JSON, missing fields, or invalid source type. |
| `405 Method Not Allowed` | N/A | Incorrect HTTP verb (e.g., POST to `/policy/current`). |
| `409 Conflict` | `CAS_MISMATCH` | Base identity mismatch due to concurrent modification. |
| `411 Length Required`| N/A | Missing `Content-Length` header on POST request. |
| `413 Payload Too Large`| N/A | Request exceeds configured byte limit (default 4MB). |
| `422 Unprocessable` | `VALIDATION_ERROR` | Syntax, IR compilation, or semantic validation failure. |
| `500 Internal Error` | `SNAPSHOT_ERROR` | Internal failure during snapshot generation. |

---

## Configuration & Security

The server is controlled via environment variables:

```bash
export BYTETAPER_ADMIN_ENABLED=1
export BYTETAPER_ADMIN_ADDRESS="127.0.0.1" # Loopback recommended for production
export BYTETAPER_ADMIN_PORT=18082
```

To completely disable the admin interface:
```bash
export BYTETAPER_ADMIN_ENABLED=0
```

---

## Restart Durability & Persistence

To ensure that dynamically applied runtime policies are preserved across process or container restarts, active policy persistence is fully supported.

### How It Works
1. **Durability Before Visibility**: When you successfully execute a `POST /admin/taperquery/apply` request with `apply` mode, the service serializes the parsed policy to a canonical, space-trimmed YAML representation and writes it atomically to disk before swapping it in memory.
2. **Atomic Writes**: Policies are written to a temporary staging file first, followed by a directory-level fsync and atomic `rename` operation. This prevents corruption in the event of partial disk/system crashes.
3. **SHA-256 Digest Verification**: The written policy is protected by SHA-256 metadata digest validation. When the server starts up, it computes the SHA-256 digest of the persisted policy file and validates it against the expected hash in the active JSON metadata file before loading the policy IR into memory.
4. **Crash Safety**:
   - If no persisted policy files are found, the server starts up gracefully using the default bootstrap policy file (`--policy-file`).
   - If persisted policy files are present but are corrupt, incomplete, or digest verification fails, the server rejects the load, prints a fatal error, and aborts startup immediately (returning exit code `1`) to prevent serving stale or undefined cache rules.

### Local-Only Multi-Pod/VM Limitation

> [!WARNING]
> Active policy persistence is strictly **local-only** and bound to the host filesystem where the ByteTaper process runs.
>
> In multi-pod (Kubernetes) or multi-VM load-balanced environments, a `POST /admin/taperquery/apply` request only applies and persists the policy to the specific pod or VM that received the HTTP request.
>
> To ensure cluster-wide consistency across multiple instances:
> 1. Use a centralized configuration manager or orchestrator to dispatch `POST /admin/taperquery/apply` requests to all running instances.
> 2. Alternatively, mount a shared network volume (e.g., ReadWriteMany) to the state directory so that all pods share the same persisted policy, although individual instances will still need a hot-reload or restart signal to load updates if they do not receive the admin API request directly.

### Environment Variable Configuration

To enable restart durability, set the following environment variables:

```bash
# Enable active policy persistence
export BYTETAPER_POLICY_PERSISTENCE_ENABLED=1

# Directory where active-policy.yaml and active-policy.meta.json will be stored
export BYTETAPER_POLICY_STATE_DIR="/workspace/bytetaper-state"
```

These environment variables can also be configured or overridden using command-line arguments:
- `--policy-state-dir <path>` or `--admin-taperquery-state-dir <path>` (Sets the state directory)
- `--disable-policy-persistence` (Explicitly disables persistence, taking highest precedence)
- `--admin-taperquery-active-policy-file <filename>` (default: `active-policy.yaml`)
- `--admin-taperquery-metadata-file <filename>` (default: `active-policy.meta.json`)

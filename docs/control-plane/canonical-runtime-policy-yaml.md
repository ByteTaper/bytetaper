# Canonical Runtime Policy YAML Format Specification

## 1. Overview

This document defines the canonical `RuntimePolicy` YAML format contract for the ByteTaper system. It establishes a strict distinction between temporary, local deployment configurations (Bootstrap YAML) and active, committed cluster policy state (Runtime Canonical YAML) stored by the Control Plane. By formalizing this schema, we ensure consistency across policy storage, validation, historical auditing, rollback, and redeploy operations.

The three types of policy document formats within ByteTaper are distinguished as follows:

| Document Type | Description |
| :--- | :--- |
| **Bootstrap YAML** | Deployment-provided starting policy (`/etc/bytetaper/policy.yaml`). Used only when no committed runtime policy exists. |
| **Runtime Canonical YAML** | Active committed policy. Produced by bootstrap import, TaperQL apply, rollback, or operator action. Stored by the Control Plane. |
| **Versioned Policy History** | Immutable historical snapshots for audit, rollback, and operator resolution. |

---

## 2. Full Canonical YAML Example

### Bootstrap-Imported (Generation 1)
```yaml
apiVersion: bytetaper.io/v1alpha1
kind: RuntimePolicy
metadata:
  namespace: default
  name: runtime
  schemaVersion: 1
  generation: 1
  policyId: sha256:11112222
  source:
    type: bootstrap-import
    applyId: bootstrap-0000000001
    appliedBy: system
    previousGeneration: 0
    previousPolicyId: ""
  bootstrap:
    file: /etc/bytetaper/policy.yaml
    role: initial-default-only
    overwriteProtection: true
    importedAtUnixEpochMs: 1779170000000
spec:
  routes: []
```

### TaperQL-Applied (Generation 2)
```yaml
apiVersion: bytetaper.io/v1alpha1
kind: RuntimePolicy
metadata:
  namespace: default
  name: runtime
  schemaVersion: 1
  generation: 2
  policyId: sha256:22223333
  source:
    type: taperql-apply
    applyId: apply-20260519-001
    appliedBy: operator
    previousGeneration: 1
    previousPolicyId: sha256:11112222
  bootstrap:
    file: /etc/bytetaper/policy.yaml
    role: initial-default-only
    overwriteProtection: true
    importedAtUnixEpochMs: 1779170000000
spec:
  routes:
    - name: users-list
      match:
        method: GET
        path: /api/v1/users
      cache:
        enabled: true
        ttlMs: 30000
        maxResponseBytes: 65536
```

---

## 3. Required Top-Level Fields

A valid `RuntimePolicy` YAML file must contain the following top-level fields:

| Field | Required | Description |
| :--- | :--- | :--- |
| `apiVersion` | Yes | Defines the API schema version. Must be `bytetaper.io/v1alpha1`. |
| `kind` | Yes | The type of resource. Must be exactly `RuntimePolicy`. |
| `metadata` | Yes | Structural metadata about the policy version, source, and identity. |
| `spec` | Yes | The actual rule specifications (routes, caching, filters). |

- **`apiVersion` Validation**: The runtime must inspect and validate this field prior to compiling the policy snapshot. Unsupported API versions must be rejected.
- **`kind` Validation**: The runtime must verify that `kind` is exactly `RuntimePolicy`. Any other value is rejected.

---

## 4. Metadata Fields

Metadata fields must be serialized in a stable order:
1. `namespace`
2. `name`
3. `schemaVersion`
4. `generation`
5. `policyId`
6. `source`
7. `bootstrap`

### Required Metadata Fields

| Field | Required | Type | Description |
| :--- | :--- | :--- | :--- |
| `namespace` | Yes | String | Policy namespace. Initially must be `default`. |
| `name` | Yes | String | Policy name. Initially must be `runtime`. |
| `schemaVersion` | Yes | Integer | Schema version for metadata/spec interpretation. Initially `1`. |
| `generation` | Yes | Integer | Monotonic operational version sequence number. |
| `policyId` | Yes | String | Deterministic identity for semantic policy content. |

### `generation` Semantics
- **Sequence**: Starts at `1` for the initial bootstrap import or first applied policy.
- **Monotonicity**: Increments by exactly `1` on every successful policy mutation (apply, import, rollback, adopt).
- **Rollbacks**: A rollback to an older policy spec does not reset the generation number. It creates a new generation (e.g. generation `3`) containing the older specification's rule contents.
- **Immutability**: Generation values in committed history must never decrease.

### `policyId` Semantics
- **Definition**: The deterministic semantic identity of the policy's rule contents.
- **Invariance**: If two policies have identical `spec` rule sets, they must have the same `policyId`.
- **Exclusion**: The `policyId` computation must **not** include volatile metadata fields (such as `generation`, `source.applyId`, `source.appliedBy`, `source.previousGeneration`, or `bootstrap.importedAtUnixEpochMs`).
- **Format**: Prefixed with hash algorithm, e.g. `sha256:<hex-string>`.

---

## 5. Source Attribution

The `metadata.source` block identifies how the active policy was created. The `source.type` field must contain one of the following values:

| Value | Description |
| :--- | :--- |
| `bootstrap-import` | Policy was imported from the local bootstrap YAML file. |
| `taperql-apply` | Policy was compiled and applied using a TaperQL query. |
| `rollback` | Policy was restored from a previous generation's specification. |
| `manual-repair` | Policy was adjusted via explicit manual operator repair. |
| `manual-adopt` | Policy was established by adopting an existing state configuration. |

### Rules
- `source.type` is required for all policies.
- For all non-initial commits (generation > 1), `source.previousGeneration` and `source.previousPolicyId` must be correctly populated to preserve the change history.
- **`appliedBy` Guidance**: Identifies the user, operator, system, or automated pipeline that triggered the change. This field may be left empty in local development or debug mode, but must be present and populated when known.
- **`applyId` Correlation**: Must be a stable correlation ID matching the TaperQL apply query invocation, manual repair task, or bootstrap import instance. This ID is used to correlate the policy change back to operational audit logs and telemetry.

---

## 6. Bootstrap Relationship

The `metadata.bootstrap` block governs how the runtime interacts with the local bootstrap file. The `bootstrap.role` field must contain one of the following values:

| Value | Description |
| :--- | :--- |
| `initial-default-only` | The bootstrap file is only loaded when no active, committed policy exists. |
| `explicit-import-only` | The bootstrap file is never auto-loaded; it must be imported via explicit API request. |

- **Overwrite Protection**: `overwriteProtection: true` is the default behavior. A deployment-provided bootstrap YAML must not overwrite a newer committed runtime policy.

---

## 7. Spec Fields

The `spec` block contains the operational routing policies.
- **Allowed Contents**: Only route declarations, caching parameters (TTL, body size limits), method criteria, paths, and field filters/admission definitions.
- **Prohibited Contents**: The `spec` block **must NOT** contain:
  - Database or storage backend configuration details.
  - Gateway-specific network transport settings.
  - Runtime container process flags.

---

## 8. Canonicalization Rules

To ensure deterministic hash identities (`policyId`) and reproducible files, the following serialization rules are enforced:
- **Stable Top-Level Field Order**: Top-level keys must be written in the following order:
  1. `apiVersion`
  2. `kind`
  3. `metadata`
  4. `spec`
- **Stable Metadata Field Order**: Keys within `metadata` must be written in the following order:
  1. `namespace`
  2. `name`
  3. `schemaVersion`
  4. `generation`
  5. `policyId`
  6. `source`
  7. `bootstrap`
- **Stable Member Ordering**: Fields within route blocks must be written in a stable, consistent sorting pattern.
- **No Comments**: Comments are stripped during canonical serialization.
- **No Anchors or Aliases**: YAML references (`&`, `*`) are prohibited.
- **Type Safety**: Booleans (`true`, `false`) and numbers must be serialized consistently without implicit type coercion ambiguities.
- **Timestamps**: All timestamps within the document must use Unix epoch milliseconds or RFC3339 format consistently.

---

## 9. `policyId` vs `canonicalHash` — Two Distinct Concepts

It is critical to distinguish between logical content identity and physical file integrity.

| Concept | Purpose | Volatile metadata included? | Location |
| :--- | :--- | :--- | :--- |
| **`policyId`** | Identifies semantic policy content. | No | Inside the canonical YAML `metadata.policyId` |
| **`canonicalHash`** | Verifies file storage integrity. | Yes (hashes the entire file) | Sidecar metadata or policy state store (NOT inside the YAML) |

> [!IMPORTANT]
> The `canonicalHash` must not be stored inside the YAML file itself. Including the file's hash within the content being hashed introduces a self-referential paradox that makes the hash impossible to compute or verify.

---

## 10. Redeploy Protection Rule

> A deployment-provided bootstrap YAML must not overwrite a newer committed runtime policy.

If the bootstrap YAML differs from the active runtime policy, the runtime must skip loading the bootstrap file. Operators detect drift through:

- Startup validation (`runtime_policy_startup_validation_test`, compose smoke)
- `PolicyMismatchDetected` lifecycle events and `bytetaper_runtime_policy_bootstrap_drift_total` ([observability.md](observability.md))
- Fleet/runtime status when local mirror does not match committed generation

ByteTaper does not auto-apply bootstrap over committed policy. See [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md).

---

## 11. Runtime Loading Order (Contract Definition)

When starting up, the ByteTaper runtime must load policy configurations in the following order of priority:

1. **Active Committed State**: Retrieve the active policy reference pointer from the Control Plane or local policy-state database.
2. **Runtime Canonical YAML**: Load the canonical policy file matching the retrieved active reference.
3. **Bootstrap YAML fallback**: Load the local bootstrap policy file *only* if no active committed policy exists.

---

## 12. Regression coverage

Canonical YAML behavior is covered by the Control Plane test matrix ([test-matrix.md](test-matrix.md)), including:

| Scenario | Tests |
|----------|-------|
| Stable emitter / identity | `policy_ir_yaml_roundtrip_test`, `taperquery_policy_persistence_test` |
| Bootstrap import → generation 1 | `runtime_policy_bootstrap_import_test` |
| Apply → monotonic generation | `control_plane_service_contract_test`, compose smoke |
| Redeploy / bootstrap protection | `runtime_policy_startup_validation_test`, compose failure smoke |
| Rollback → new generation | `manual_resolution_service_test` |

---

## 13. Non-Goals

This specification defines the schema layout and operational rules. The following items are explicitly out of scope for this phase:
- Writing or refactoring C++ parser, compiler, or YAML emitters.
- Modifying RocksDB storage layouts or schema versions.
- Updating Admin HTTP endpoints or the TaperQuery API implementation.
- Migrating existing state files to the new canonical format.

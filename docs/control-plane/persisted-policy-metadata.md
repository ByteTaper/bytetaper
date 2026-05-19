# Persisted Policy Metadata Sidecar Format Specification

## 1. Overview

This document defines the persisted `RuntimePolicy` metadata JSON sidecar schema (`active-policy.meta.json`) used by ByteTaper for durability, recovery integrity checks, and lineage tracking.

While the canonical YAML specification (`active-policy.yaml`) describes the runtime configuration behavior, the JSON metadata sidecar describes persisted storage state, integrity hashes, and operational metadata. It is used during system startup and update validation transactions to detect corruptions, manual modifications, and lineage divergence.

---

## 2. Complete Metadata JSON Schema Example

### Schema Version 1 (Extended)
```json
{
  "metadataSchemaVersion": 1,
  "resourceKey": "policy/default/runtime",

  "generation": 13,
  "policyId": "sha256:abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234abcd1234",
  "canonicalHash": "sha256:1111222233334444555566667777888899990000aaaabbbbccccddddeeeeffff",
  "canonicalHashAlgorithm": "sha256",

  "sourceType": "taperql-apply",
  "applyId": "apply-20260519-001",
  "operatorId": "admin",
  "requestId": "req-20260519-001",

  "previousGeneration": 12,
  "previousPolicyId": "sha256:prev1234prev1234prev1234prev1234prev1234prev1234prev1234prev1234",

  "schemaVersion": 1,
  "apiVersion": "bytetaper.io/v1alpha1",
  "kind": "RuntimePolicy",

  "activePolicyFile": "active-policy.yaml",
  "versionedPolicyFile": "versions/0000000013-sha256_abcd1234.yaml",

  "writtenAtUnixEpochMs": 1779170000000,
  "committedAtUnixEpochMs": 1779170000000,

  "bootstrap": {
    "file": "/etc/bytetaper/policy.yaml",
    "role": "initial-default-only",
    "overwriteProtection": true
  },

  "compatibility": {
    "policyIrVersion": "tq-ir/v1",
    "identityVersion": "policy-identity/v2",
    "emitterVersion": "canonical-yaml/v1",
    "runtimeMinVersion": "v1",
    "runtimeCapabilityProfile": "baseline"
  }
}
```

---

## 3. Schema Fields

### Required Fields

A valid Schema Version 1 metadata file must contain the following fields:

| JSON Key | Type | Description |
| :--- | :--- | :--- |
| `metadataSchemaVersion` | Integer | Schema version of the metadata format. Must be `1`. |
| `resourceKey` | String | Logical identity of the policy resource. Default is `policy/default/runtime`. |
| `generation` | Integer | Monotonic version number of the policy change sequence. |
| `policyId` | String | Deterministic semantic content hash (`sha256:<hex>`). |
| `canonicalHash` | String | File integrity hash of the canonical YAML bytes (`sha256:<hex>`). |
| `canonicalHashAlgorithm` | String | Algorithm used to compute the YAML file integrity. Must be `sha256`. |
| `sourceType` | String | Operational source type of the change (see Allowed Source Types). |
| `schemaVersion` | Integer | Canonical runtime policy schema version. |
| `apiVersion` | String | API version of the canonical YAML. Must be `bytetaper.io/v1alpha1`. |
| `kind` | String | Kind of the canonical YAML. Must be `RuntimePolicy`. |
| `activePolicyFile` | String | Relative filepath to the active policy YAML file. |
| `writtenAtUnixEpochMs` | Integer | Millisecond timestamp indicating when the metadata was written. |

### Optional / Conditional Fields

| JSON Key | Type | Required When | Description |
| :--- | :--- | :--- | :--- |
| `applyId` | String | Source is apply/rollback/repair/adopt | Correlates metadata with transaction logs. |
| `operatorId` | String | Known | Identity of the user/process making the change. |
| `requestId` | String | Known | Request correlation ID. |
| `previousGeneration` | Integer | Generation > 1 | Generation of the preceding active policy version. |
| `previousPolicyId` | String | Generation > 1 | Content hash identity of the preceding active policy. |
| `versionedPolicyFile` | String | History enabled | Relative path to the immutable versioned history file. |
| `committedAtUnixEpochMs`| Integer | Commit confirmed | Millisecond timestamp of commit acknowledgment. |
| `bootstrap` | Object | Bootstrap tracked | Sub-structure tracking bootstrap YAML parameters. |
| `compatibility` | Object | Known | Compiler, emitter, and runtime capability constraints. |

---

## 4. Field Semantics & Constraints

### `sourceType` Enum Values

`sourceType` tracks the lineage of a configuration change. It is validated against the following strict list:

| Allowed Value | Definition |
| :--- | :--- |
| `bootstrap-import` | Policy was initialized from the static local deployment YAML. |
| `taperql-apply` | Policy was applied dynamically via the TaperQL Apply API. |
| `rollback` | Policy was restored via an explicit rollback command. |
| `manual-repair` | Policy was restored or corrected via operator repair procedures. |
| `manual-adopt` | Policy was verified and manually adopted into the control stream. |
| `unknown` | State source lineage is legacy or untraceable. |

### `activePolicyFile` Path Safety

To prevent path traversal exploits:
- File paths must be strictly relative to the persistence state directory.
- Absolute paths (starting with `/`) are rejected.
- Directory traversal components (like `..`) are rejected.

---

## 5. Validation Logic

During start-up and policy load operations, the persistence module validates metadata against the canonical YAML file.

### Integrity Validation

1. The checksum of the YAML file is recomputed.
2. The recomputed checksum must match the `canonicalHash` property (with or without the `sha256:` prefix).

### Cross-Validation Validation

Metadata and YAML header values must strictly match:

```text
metadata.generation == YAML.metadata.generation
metadata.policyId == YAML.metadata.policyId
metadata.schemaVersion == YAML.metadata.schemaVersion
metadata.apiVersion == YAML.apiVersion
metadata.kind == YAML.kind
```

Failure to satisfy integrity or cross-validation checks triggers a `METADATA_CANONICAL_HASH_MISMATCH` or specific property mismatch error, and prevents the policy from loading.

---

## 6. Backward Compatibility

For legacy metadata files missing the `metadataSchemaVersion` property:
- The parser falls back to Schema Version 0.
- Legacy fields are mapped as follows:
  - `policy_identity` maps to `policyId`.
  - `canonical_yaml_sha256` maps to `canonicalHash`.
  - `active_policy_file` maps to `activePolicyFile`.
- Path safety checks still apply to `active_policy_file`.

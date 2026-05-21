# Control Plane Observability

## Overview

Control Plane observability includes structured lifecycle logs, durable audit records, Prometheus metrics, and fleet counters. Metrics are split across two emitters:

| Emitter | Prefix | Process | Source |
|---------|--------|---------|--------|
| Control Plane | `bytetaper_control_plane_*` | CP (`19090`) | [`control_plane_metrics.cpp`](../../src/control_plane/control_plane_metrics.cpp) |
| Runtime Policy Plane | `bytetaper_runtime_policy_*` | Runtime (`18083` in compose) | [`runtime_policy_metrics.cpp`](../../src/runtime_policy/runtime_policy_metrics.cpp) |

Use CP metrics for apply queue, fleet aggregates, and auth. Use runtime policy metrics for **activation lifecycle**, **cache cleanup**, **local mismatch**, and **per-runtime convergence** signals.

Verified in `control_plane_metrics_test.cpp` and `runtime_policy_metrics_test.cpp`.

## Structured lifecycle logs

JSON lifecycle events via `format_control_plane_lifecycle_log_json()` — [`control_plane_log_events.h`](../../include/control_plane/control_plane_log_events.h).

### Event types

| Event | Description |
|-------|-------------|
| `PolicyDryRunRequested` | Dry-run started |
| `PolicyApplySubmitted` | Apply accepted |
| `PolicyApplyQueued` | Job enqueued |
| `PolicyApplyProcessing` | Worker processing |
| `PolicyApplyCandidateBuilt` | Candidate compiled |
| `PolicyVersionStored` | Immutable version written |
| `PolicyActivePromoted` | Active pointer promoted |
| `PolicyApplyCommitted` | Transaction committed |
| `PolicyApplyFailed` | Apply failed |
| `PolicyActivationStarted` | Barrier started |
| `PolicyActivationSucceeded` | Barrier succeeded |
| `PolicyActivationFailed` | Barrier failed |
| `RuntimeConverged` | Runtime matched committed |
| `RuntimeStale` | Runtime stale |
| `RuntimeActivationFailed` | Runtime activation failed |
| `PolicyMismatchDetected` | Mismatch detected |
| `ManualRepairRequested` / `ManualRepairCompleted` | Repair lifecycle |
| `ManualAdoptRequested` / `ManualAdoptCompleted` | Adopt lifecycle |
| `PolicyRollbackRequested` / `PolicyRollbackCompleted` | Rollback lifecycle |

### Common log fields

| Field | Description |
|-------|-------------|
| `resource_key` | Policy resource |
| `job_id` | Apply job id |
| `request_id`, `operator_id` | Operator correlation |
| `before_generation`, `after_generation` | Generation lineage |
| `before_policy_id`, `after_policy_id` | Identity lineage |
| `stage`, `status`, `error_code`, `message` | Outcome detail |
| `duration_ms` | Operation duration |
| `manual_resolution_required` | Operator action needed |
| `old_snapshot_still_active` | Failed activation kept prior snapshot |

Audit persistence: `PolicyLifecycleEmitter::emit_and_persist_audit` → `PolicyStateStore::append_audit_record`.

## Prometheus metrics

Metric names are defined in [`render_control_plane_metrics_prometheus`](../../src/control_plane/control_plane_metrics.cpp). Verified in `control_plane_metrics_test.cpp`.

### Apply and store

| Metric | Description |
|--------|-------------|
| `bytetaper_control_plane_policy_apply_total` | Apply attempts |
| `bytetaper_control_plane_policy_apply_failed_total` | Apply failures |
| `bytetaper_control_plane_policy_apply_duration_ms` | Apply duration sum |
| `bytetaper_control_plane_policy_cas_conflict_total` | CAS conflicts |
| `bytetaper_control_plane_policy_version_stored_total` | Versions stored |
| `bytetaper_control_plane_active_pointer_promote_total` | Promotions |
| `bytetaper_control_plane_active_pointer_promote_failed_total` | Promotion failures |
| `bytetaper_control_plane_policy_apply_stage_duration_ms` | Per-stage durations |

### Queue

| Metric | Description |
|--------|-------------|
| `bytetaper_control_plane_policy_update_queue_depth` | Current depth |
| `bytetaper_control_plane_policy_update_queue_capacity` | Capacity |
| `bytetaper_control_plane_policy_update_queue_rejected_total` | Rejections |
| `bytetaper_control_plane_policy_update_job_total` | Jobs processed |
| `bytetaper_control_plane_policy_update_job_failed_total` | Job failures |
| `bytetaper_control_plane_policy_update_job_duration_ms` | Job duration sum |
| `bytetaper_control_plane_policy_update_worker_active` | Active workers |

### Manual operations

| Metric | Description |
|--------|-------------|
| `bytetaper_control_plane_manual_repair_total` | Repair attempts |
| `bytetaper_control_plane_manual_repair_failed_total` | Repair failures |
| `bytetaper_control_plane_manual_adopt_total` | Adopt attempts |
| `bytetaper_control_plane_manual_adopt_failed_total` | Adopt failures |
| `bytetaper_control_plane_policy_rollback_total` | Rollbacks |
| `bytetaper_control_plane_policy_rollback_failed_total` | Rollback failures |

### Fleet

| Metric | Description |
|--------|-------------|
| `bytetaper_control_plane_fleet_runtime_count` | Registered runtimes |
| `bytetaper_control_plane_fleet_converged_count` | Converged runtimes |
| `bytetaper_control_plane_fleet_stale_count` | Stale runtimes |
| `bytetaper_control_plane_fleet_failed_count` | Failed runtimes |
| `bytetaper_control_plane_fleet_unreachable_count` | Unreachable runtimes |
| `bytetaper_control_plane_fleet_degraded_count` | Degraded runtimes |
| `bytetaper_control_plane_fleet_converged` | Fleet-level flag (0/1) |

### Security

| Metric | Description |
|--------|-------------|
| `bytetaper_control_plane_auth_requests_total` | Auth checks |
| `bytetaper_control_plane_auth_denied_total` | Denials |
| `bytetaper_control_plane_mutation_rejected_total` | Mutation rejections |
| `bytetaper_control_plane_public_bind_rejected_total` | Public bind rejections |
| `bytetaper_control_plane_unsafe_config_total` | Unsafe config detections |
| `bytetaper_control_plane_runtime_only_mutation_rejected_total` | Runtime-only violations |

### Runtime policy metrics (per runtime)

Emitted by [`render_runtime_policy_metrics_prometheus`](../../src/runtime_policy/runtime_policy_metrics.cpp). Recorded from the activation barrier, pull loop, and startup mismatch classifier.

Scrape target in compose: runtime metrics HTTP (`BYTETAPER_RUNTIME_METRICS_URL`, e.g. `http://127.0.0.1:18083/metrics`).

#### Activation lifecycle

| Metric | Type | Description |
|--------|------|-------------|
| `bytetaper_runtime_policy_activation_total` | counter | Successful activation barrier completions |
| `bytetaper_runtime_policy_activation_failed_total` | counter | Activation barrier failures |
| `bytetaper_runtime_policy_activation_duration_ms` | counter | Sum of successful activation durations (ms) |
| `bytetaper_runtime_policy_activation_stage_duration_ms` | counter | Per-stage duration sum (emitted per stage index when `count > 0`; stage index not exposed as a label — use lifecycle logs for stage forensics) |

Stage indices map to `PolicyActivationStage`: `Committed`, `OperationalDiffComputed`, `CacheNamespacePrepared`, `RouteEpochsBumped`, `CleanupEnqueued`, `MaterializedVariantsInvalidated`, `SnapshotBuilt`, `SnapshotSwapped`, `ActivationPublished`, `Activated`.

#### Activation state (gauges)

| Metric | Values | Description |
|--------|--------|-------------|
| `bytetaper_runtime_policy_active_generation` | generation number | Last successfully activated generation |
| `bytetaper_runtime_policy_activation_status` | `0` unknown, `1` active, `2` failed | Current activation status |
| `bytetaper_runtime_policy_cleanup_pending` | `0` / `1` | Background L2/variant cleanup still pending after activation |

#### Cache cleanup (activation barrier)

| Metric | Description |
|--------|-------------|
| `bytetaper_runtime_policy_route_epochs_bumped_total` | Route cache epochs bumped during activation |
| `bytetaper_runtime_policy_l1_cleanup_enqueued_total` | L1 cleanup jobs enqueued |
| `bytetaper_runtime_policy_l2_cleanup_enqueued_total` | L2 cleanup jobs enqueued |
| `bytetaper_runtime_policy_materialized_variant_invalidated_total` | Materialized field variants invalidated |

See [activation-barrier.md](activation-barrier.md) and [cache-synchronization.md](cache-synchronization.md).

#### Mismatch (local mirror vs committed)

| Metric | Description |
|--------|-------------|
| `bytetaper_runtime_policy_mismatch_total` | All local mirror mismatch events (any reason) |

Reason-specific counters increment in addition to `mismatch_total` based on `record_mismatch()` reason string:

| Metric | Reason strings (examples) |
|--------|---------------------------|
| `bytetaper_runtime_policy_corrupted_local_total` | `local_policy_corrupted_or_modified`, `corrupted` |
| `bytetaper_runtime_policy_stale_local_total` | `stale`, `runtime_stale` |
| `bytetaper_runtime_policy_diverged_local_total` | `diverged` |
| `bytetaper_runtime_policy_missing_local_total` | `missing` |
| `bytetaper_runtime_policy_unparseable_local_total` | `unparseable` |
| `bytetaper_runtime_policy_bootstrap_drift_total` | `bootstrap_drift` |

Mismatch metrics align with `PolicyMismatchDetected` lifecycle events and [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md). ByteTaper does not auto-repair — rising mismatch counters require operator action.

#### Runtime convergence (per-runtime pull loop)

| Metric | `record_runtime_convergence` status | Description |
|--------|-------------------------------------|-------------|
| `bytetaper_runtime_policy_runtime_converged_total` | `converged` | Local active matches committed |
| `bytetaper_runtime_policy_runtime_stale_total` | `stale` | Behind committed generation |
| `bytetaper_runtime_policy_runtime_activation_failed_total` | `failed` | Activation failed during pull |

Fleet-level aggregates remain on `bytetaper_control_plane_fleet_*` counters — see [fleet-convergence.md](fleet-convergence.md).

### Scrape note

Control Plane and runtime processes start a metrics HTTP server.

- **CP:** `render_control_plane_metrics_prometheus` defines the `bytetaper_control_plane_*` contract.
- **Runtime:** `render_runtime_policy_metrics_prometheus` defines the `bytetaper_runtime_policy_*` contract; compose exposes runtime at `:18083/metrics` alongside data-path runtime metrics.

If a metric family is missing from your scrape output, prefer structured lifecycle JSON logs and the audit store for forensics — metric names in this document remain the implementation contract.

## Label cardinality guidance

- Do **not** put `policy_id`, `route_id`, or `runtime_id` as Prometheus labels on high-frequency counters
- Use logs and audit records for per-identity forensics
- Fleet gauges are low-cardinality aggregates suitable for alerting

## Alerting suggestions

| Signal | Condition |
|--------|-----------|
| Apply failures | Rate of `bytetaper_control_plane_policy_apply_failed_total` |
| CAS storms | `bytetaper_control_plane_policy_cas_conflict_total` spike |
| Fleet not converged | `bytetaper_control_plane_fleet_converged == 0` sustained |
| Stale runtimes (fleet) | `bytetaper_control_plane_fleet_stale_count > 0` after apply grace period |
| Auth denials | `bytetaper_control_plane_auth_denied_total` spike |
| Activation failures | `bytetaper_runtime_policy_activation_failed_total` or `activation_status == 2` |
| Cleanup stuck | `bytetaper_runtime_policy_cleanup_pending == 1` sustained |
| Local mismatch | Any rise in `bytetaper_runtime_policy_mismatch_total` or reason counters (`stale_local`, `diverged_local`, `corrupted_local`, …) |
| Runtime not converged | `bytetaper_runtime_policy_runtime_stale_total` or `runtime_activation_failed_total` after apply |

## Related docs

- [activation-barrier.md](activation-barrier.md)
- [fleet-convergence.md](fleet-convergence.md)
- [runtime-policy-plane.md](runtime-policy-plane.md)
- [test-matrix.md](test-matrix.md)
- [security.md](security.md)

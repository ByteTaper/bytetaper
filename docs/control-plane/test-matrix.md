# Control Plane Test Matrix

This document maps requirements to existing tests and runner tiers.

## How to run

| Tier | Command | Scope |
|------|---------|--------|
| Unit | `make test-control-plane-unit` or `./scripts/test/control-plane-unit.sh` | Fast GTest: store, queue, guardrails, transactions |
| Integration | `make test-control-plane-integration` | Multi-component GTest with RocksDB, workers, pull loop |
| Compose | `make test-control-plane-compose` | Docker profile smoke + failure flows (heavy) |
| Full suite | `make test` | All project unit tests (includes CP) |

Reset persistent Compose state:

```bash
docker compose -f docker-compose.yml -f docker-compose.control-plane.yml down -v
```

## 1. PolicyStateStore regression

| Requirement | Test |
|-------------|------|
| Store/load policy version | `rocksdb_policy_state_store_test` — `StoreAndLoadPolicyVersion` |
| Promote active pointer | `PromoteActiveFromEmptySucceeds`, `PromoteActiveMatchingExpectedSucceeds` |
| Reject stale CAS | `PromoteActiveStaleFails` |
| Version conflict | `StoreSameVersionDifferentHashFails` |
| Audit survives restart | `AuditRecordAppendSucceeds` |
| Active target missing | `ActivePointerTargetMissingDetected` |
| Restart durability | `StoreSurvivesCloseReopen`, `control_plane_restart_durability_test` |

## 2. Apply pipeline / queue

| Requirement | Test |
|-------------|------|
| Job created | `control_plane_service_contract_test` — `ApplyAcceptedWithValidCas` |
| State transitions | `ApplyJobObservesSubmittedQueuedProcessingCommitted` |
| FIFO per resourceKey | `policy_update_queue_concurrency_test` — `FifoOrderPerResourceWithOneWorker` |
| Concurrent shards | `DifferentResourcesApplyConcurrently` |
| Shard mapping stable | `policy_update_queue_test` — `StableShardIdForResourceKey`, `WorkerCountDoesNotAffectShardId` |
| Queue full | `policy_update_queue_test` — `QueueFullRejectsJob` |
| CAS conflict | `policy_apply_transaction_test` — `CasMismatchFailsAtCompareAndPromote` |
| Compile failure | `CompileFailureFailsBeforeStoreVersion` |
| Store failure | `StoreVersionConflictFails` |

## 3. Operational activation barrier

| Requirement | Test |
|-------------|------|
| Field allowlist reduction / epoch | `policy_activation_barrier_test` — `FieldReductionBumpsEpochBeforeSwap` |
| Cache TTL change | `policy_operational_sync_test` — `CacheTtlChangeRequiresEpochBump` |
| Route removal cleanup | `RouteRemovedEnqueuesCleanup` |
| Variant identity change | `MaterializedVariantChangeTriggersVariantInvalidation` |
| Snapshot swap / failure preserves old | `SuccessfulActivationSwapsSnapshot`, `SnapshotBuildFailurePreservesOldSnapshot` |
| Stale L1 not served | `ServingSafeAfterFieldReductionInvalidatesStaleL1` |
| Compose field filter E2E | `tests/integration/e2e/control_plane_field_allowlist_e2e_test.py` |

## 4. Runtime pull and convergence

| Requirement | Test |
|-------------|------|
| Pull committed generation | `runtime_policy_pull_loop_integration_test` — `ConvergesToCommittedGenerationTwo` |
| No-op when matched | `runtime_policy_pull_loop_test` — `RemoteGenerationEqualsLocalIsNoOp` |
| Fetch newer / activate | `RemoteGenerationNewerFetchesAndActivates` |
| Hash mismatch | `HashMismatchRejectsActivation` |
| Activation failure keeps old | `ActivationBarrierFailurePreservesOldSnapshot` |
| CP unavailable LKG | `ControlPlaneUnavailableContinuesLastKnownGood` |
| Status report | `StatusReportContainsActiveGenerationAndPolicyId` |
| Fleet converged / stale | `fleet_status_integration_test` — `TwoRuntimesConvergeAfterStaleReport` |

## 5. Restart durability

| Requirement | Test |
|-------------|------|
| CP active pointer after restart | `control_plane_restart_durability_test` |
| Runtime mirror + reconverge | `runtime_policy_pull_loop_integration_test` — `RestartLoadsMirrorThenConvergesToGenerationThree` |
| Bootstrap no overwrite | `runtime_policy_bootstrap_import_test`, compose restart step |

## 6. Mismatch / corruption (no auto-recovery)

| Requirement | Test |
|-------------|------|
| Classifiers | `runtime_policy_startup_validation_test` — classifier + integration cases |
| Tampered mirror | `manual_resolution_integration_test` — `RepairLocalRestoresTamperedMirror` |
| No restore from versions | `CorruptLocalNotRestoredFromVersions` |
| Compose tamper step | `control-plane-compose.sh` failure flow |

## 7. Manual resolution

| Requirement | Test |
|-------------|------|
| repair / adopt / rollback | `manual_resolution_service_test` |
| Integration paths | `manual_resolution_integration_test` |
| Audit records | `SuccessfulManualOperationsAppendAuditRecords` |

## 8. Security guardrails

| Requirement | Test |
|-------------|------|
| Bind / production | `control_plane_guardrails_test`, `control_plane_security_integration_test` |
| Runtime-only mutation | `control_plane_security_test` — `ServiceApplyRejectedWhenRuntimeOnly` |
| Static token auth | `StaticTokenValidAllowsProductionMutation`, `StaticTokenInvalidRejectsProductionMutation` |
| Tokens not logged | `StaticTokenInvalidRejectsProductionMutation`, `AuthDenialSecurityLogOmitsBearerToken` |
| Dangerous confirm | `control_plane_security_integration_test` — rollback confirm |

## 9. Docker Compose integration

| Step | Script |
|------|--------|
| Full success + failure flow | `scripts/test/control-plane-compose.sh` (strict `fleet.converged`, post-restart re-convergence, CP reconnect, stale mirror tamper + runtime restart + `poll_fleet_converged`, `assert_no_committed_adopt_of_stale_mirror`; compose profile uses `BYTETAPER_POLICY_PERSISTENCE_ENABLED=0` so explicit fleet-mismatch-after-tamper is not the default gate) |
| Field allowlist via CP + Envoy | `control_plane_field_allowlist_e2e_test.py` (fixtures under `tests/fixtures/control-plane/`); invoked by default at the end of `control-plane-compose.sh` (`BYTETAPER_FIELD_E2E_SKIP_OPEN_APPLY=1`) so tamper/restart steps stay on generation 2 — reduction applies `field_variant.include_fields` (wired to route `field_filter` allowlist) and asserts sensitive fields are absent from the live Envoy `/api/v1/small` response |
| Demo (subset) | `scripts/demo/control-plane-demo.sh` |
| Profile docs | [docker-compose-profile.md](docker-compose-profile.md) |

## CI notes

- Default `make test` runs all GTest labels; CP unit + integration stay in the main Docker unit-test image.
- `test-control-plane-compose` is intended for manual, nightly, or release smoke (poll-based, not fixed sleeps).
- Use unique volume names or `down -v` between runs to avoid stale RocksDB / mirror state.

# Control Plane Production Readiness Checklist

Use this checklist before running ByteTaper Control Plane + Runtime in a production-like environment. Not all items require full HA — initial release is single-writer CP with explicit limitations.

## Architecture

- [ ] Deployment uses **Control Plane + Runtime** topology for multi-pod or multi-VM ([deployment-guardrails.md](deployment-guardrails.md))
- [ ] Single-runtime local mode is **not** used for multi-instance fleets
- [ ] Five-plane boundaries understood by operators ([architecture-boundary.md](architecture-boundary.md))
- [ ] **No auto-repair** policy documented for on-call ([manual-resolution runbook](../runbooks/control-plane/manual-resolution.md))

## Storage

- [ ] `RocksDBPolicyStateStore` on durable volume (`BYTETAPER_POLICY_STATE_DB`)
- [ ] Policy-state DB path **separate** from runtime L2 cache RocksDB
- [ ] **Single Control Plane writer** — no multiple pods opening same DB ([rocksdb-policy-state-store.md](rocksdb-policy-state-store.md))
- [ ] Operators understand: **not etcd, not consensus, not RWX multi-pod**

## Persistence

- [ ] Backup procedure for `policy-state-db` documented and tested
- [ ] Restore drill performed (stop CP → restore → verify `policy/current`)
- [ ] Versioned history and active pointer recoverable after restore
- [ ] Restart durability verified (`make test-control-plane-integration` or equivalent)

## Security

- [ ] `BYTETAPER_DEPLOYMENT_MODE=production` with explicit security flags
- [ ] Static bearer token (`BYTETAPER_CONTROL_PLANE_TOKEN`) or future auth configured
- [ ] Control Plane admin (`19090`) not exposed to public Internet without gateway auth
- [ ] `allow_public_bind` only if intentional; firewalled
- [ ] TaperQuery admin (`18082`) not exposed in multi-runtime deployments
- [ ] Tokens never logged ([security.md](security.md))

## Runtime convergence

- [ ] All runtimes `runtime-only` with `BYTETAPER_CONTROL_PLANE_ENDPOINT` set
- [ ] `BYTETAPER_POLICY_PULL_ENABLED=1`
- [ ] Fleet status monitored (`GET /admin/control-plane/fleet/status`)
- [ ] Alert on sustained `fleet.converged == false`
- [ ] Pull interval appropriate for RPO/RTO targets

## Activation safety

- [ ] Operators understand **persisted ≠ activated** ([activation-barrier.md](activation-barrier.md))
- [ ] Apply success validated via fleet convergence, not only HTTP `Accepted`
- [ ] Field-filter / allowlist changes tested on data path (compose E2E pattern)
- [ ] `PolicyActivationFailed` runbook steps understood

## Observability

- [ ] Lifecycle JSON logs collected
- [ ] Audit records retained with store backups
- [ ] Prometheus metrics scraped where available ([observability.md](observability.md))
- [ ] Low-cardinality labels only — no per-route/per-policy_id metric labels
- [ ] Dashboards: apply rate, CAS conflicts, fleet stale/failed counts

## Backup / restore

- [ ] Filesystem or volume snapshot of `policy-state-db`
- [ ] Runtime LKG mirror (`BYTETAPER_POLICY_STATE_DIR`) backup optional (rebuild via pull)
- [ ] Restore runbook tested

## Upgrade / rollback

- [ ] Apply uses CAS (`expected_base_generation`, `expected_base_policy_id`)
- [ ] Rollback procedure tested (`plan_rollback` → `rollback` with `confirm`)
- [ ] Rollback creates **new** generation (monotonic counter)
- [ ] Binary upgrade: CP stopped during store restore if downgrading store format

## Incident response

- [ ] [Manual resolution runbook](../runbooks/control-plane/manual-resolution.md) linked in on-call docs
- [ ] CP unavailable: LKG serving expected ([runtime-pull-loop.md](runtime-pull-loop.md))
- [ ] Divergent runtime: repair vs adopt decision tree understood
- [ ] Corrupt store: restore from backup, not multi-writer repair

## Known limitations (initial release)

- [ ] **Single-writer `RocksDBPolicyStateStore`** — one CP instance
- [ ] **Control Plane is not HA** — plan for CP downtime and LKG serving
- [ ] **No etcd / PostgreSQL / object storage backend** yet
- [ ] **Runtime pods must not mutate global policy** in production
- [ ] **CP must not be public** without explicit security configuration
- [ ] Manual resolution APIs on in-process service — HTTP routes limited ([api.md](api.md))

## Future HA path

Document only — not required for initial pilot sign-off:

1. External `PolicyStateStore` with transactional semantics (etcd, SQL, object store + index)
2. Stateless CP replicas behind load balancer
3. mTLS/OIDC for operator and runtime ↔ CP traffic
4. Multi-gateway adapter fleet status

See [architecture-boundary.md](architecture-boundary.md) §12 and [rocksdb-policy-state-store.md](rocksdb-policy-state-store.md).

## Verification commands

```bash
make test-control-plane-unit
make test-control-plane-integration
make test-control-plane-compose   # heavy
./scripts/demo/control-plane-demo.sh
```

Test mapping: [test-matrix.md](test-matrix.md).

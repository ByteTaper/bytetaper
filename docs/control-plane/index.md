# Control Plane Documentation

Operator and developer documentation for the ByteTaper Control Plane — committed policy lifecycle, fleet convergence, and production readiness.

## Five-plane model

```
Admin / CI / GitOps
  ↓
Control Plane
  ↓
Policy State Store
  ↓
Runtime Policy Plane
  ↓
Operational Plane
  ↓
Data Plane Core
  ↓
Gateway Adapters (Envoy / future: Nginx, HAProxy, Kong)
```

| Plane | Owns | Must not |
|-------|------|----------|
| **Control Plane** | Global policy mutation, versioning, audit, fleet status | Serve live traffic; depend on gateway APIs |
| **Runtime Policy Plane** | Pull/consume committed policy, compile snapshots, LKG mirror | Own global writes |
| **Operational Plane** | Activation barrier, cache epoch/L1/L2 sync | Mutate global active generation |
| **Data Plane Core** | Cache, transform, pagination on immutable snapshot | Read YAML on hot path; call Control Plane per request |
| **Gateway Adapters** | Protocol translation (ext_proc, etc.) | Own policy compiler or cache invalidation rules |

**Core rule:** Control Plane owns committed policy. Runtime does **not** auto-repair mismatched local state — manual operator action is required. See [manual-resolution runbook](../runbooks/control-plane/manual-resolution.md).

## Documentation map

### Architecture

| Doc | Description |
|-----|-------------|
| [architecture-boundary.md](architecture-boundary.md) | Plane boundaries, deployment model, guardrails summary |
| [runtime-policy-plane.md](runtime-policy-plane.md) | Pull loop, LKG mirror, snapshot consumption |
| [operational-plane-sync.md](operational-plane-sync.md) | Activation barrier and cache coordination |
| [runtime-convergence.md](runtime-convergence.md) | Fleet classification (stale, diverged, failed, …) |

### Policy format and storage

| Doc | Description |
|-----|-------------|
| [canonical-runtime-policy-yaml.md](canonical-runtime-policy-yaml.md) | `RuntimePolicy` YAML schema |
| [persisted-policy-metadata.md](persisted-policy-metadata.md) | `active-policy.meta.json` sidecar |
| [versioned-policy-history.md](versioned-policy-history.md) | Immutable version files and rollback semantics |
| [policy-state-store.md](policy-state-store.md) | `PolicyStateStore` abstraction |
| [rocksdb-policy-state-store.md](rocksdb-policy-state-store.md) | RocksDB backend, single-writer limits |

### APIs and operations

| Doc | Description |
|-----|-------------|
| [api.md](api.md) | HTTP vs in-process API index, auth, resource keys |
| [apply-api.md](apply-api.md) | Apply pipeline, CAS, job states |
| [fleet-status-api.md](fleet-status-api.md) | Fleet status and runtime status report |
| [manual-resolution-api.md](manual-resolution-api.md) | Repair, adopt, rollback (service API) |
| [activation-barrier.md](activation-barrier.md) | Persisted vs activated |
| [cache-synchronization.md](cache-synchronization.md) | Epoch bump, L1/L2 cleanup |
| [runtime-pull-loop.md](runtime-pull-loop.md) | Runtime pull and CP-unavailable behavior |
| [fleet-convergence.md](fleet-convergence.md) | Convergence states and error codes |

### Deployment, security, observability

| Doc | Description |
|-----|-------------|
| [docker-compose-profile.md](docker-compose-profile.md) | Local Control Plane + Runtime profile |
| [security.md](security.md) | Auth, tokens, bind guardrails |
| [deployment-guardrails.md](deployment-guardrails.md) | Deployment modes and roles |
| [observability.md](observability.md) | Metrics, audit, lifecycle logs |
| [test-matrix.md](test-matrix.md) | Regression test tiers |
| [production-readiness-checklist.md](production-readiness-checklist.md) | GA readiness checklist |

### Runbooks

| Doc | Description |
|-----|-------------|
| [manual-resolution.md](../runbooks/control-plane/manual-resolution.md) | Operator procedures for mismatch recovery |

## Quick start

```bash
# Start Control Plane + Runtime profile
LOCAL_UID=$(id -u) LOCAL_GID=$(id -g) \
  docker compose -f docker-compose.yml -f docker-compose.control-plane.yml \
  up -d --build bytetaper-control-plane bytetaper-runtime mock-api envoy

# Smoke demo
./scripts/demo/control-plane-demo.sh

# Regression tiers
make test-control-plane-unit
make test-control-plane-integration
make test-control-plane-compose
```

## Deployment modes

| Mode | Use when |
|------|----------|
| **Single-runtime / local** | One process; optional TaperQuery admin on `18082` |
| **Control Plane + Runtime** | Multiple runtimes, shared committed policy, fleet convergence — **recommended** for multi-pod or multi-VM |

See [deployment-guardrails.md](deployment-guardrails.md) and [README.md](../../README.md).

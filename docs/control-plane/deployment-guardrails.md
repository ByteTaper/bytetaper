# Deployment Guardrails

## Deployment modes

`ControlPlaneDeploymentMode` ([`control_plane_deployment_mode.h`](../../include/control_plane/control_plane_deployment_mode.h)):

| Mode | String | Typical use |
|------|--------|-------------|
| `LocalDev` | `local-dev` | Developer workstation |
| `SingleNode` | `single-node` | All-in-one process |
| `MultiRuntime` | `multi-runtime` | CP + one or more runtimes (compose) |
| `Production` | `production` | Hardened auth and startup validation |

Parse from `BYTETAPER_DEPLOYMENT_MODE`.

## Process roles

| Role | Env | Behavior |
|------|-----|----------|
| **Control Plane** | `BYTETAPER_RUNTIME_ROLE=control-plane` | Owns `policy-state-db`, serves `/admin/control-plane/*` |
| **Runtime only** | `BYTETAPER_RUNTIME_ROLE=runtime-only` | Pulls policy; mutations rejected locally |

## Startup validation

Production and multi-runtime modes **fail startup** on validation errors (`should_fail_startup_on_validation_errors`).

Common failures:

- Control plane enabled without policy state store (CP role)
- `runtime-only` without `control_plane_endpoint` (multi-runtime / production)
- Production without explicit `enabled` / `runtime_role` flags
- Production without auth (unless explicitly allowed)
- Public bind without `allow_public_bind`

Warnings (non-production): public bind without allow.

## When to use which topology

| Topology | Recommended for |
|----------|-----------------|
| **Single-runtime** | Local dev, single binary, TaperQuery admin on `18082` |
| **Control Plane + Runtime** | Multiple pods/VMs, shared committed policy, fleet convergence, future multi-gateway |

ByteTaper can run in:

- **Single-runtime local mode** — one process, optional `/admin/taperquery/*`
- **Control Plane + Runtime mode** — dedicated CP with `policy-state-db`, runtimes pull — recommended for multi-pod, multi-VM, and future multi-gateway adapters

## Environment reference (compose)

### Control Plane pod

| Variable | Example |
|----------|---------|
| `BYTETAPER_RUNTIME_ROLE` | `control-plane` |
| `BYTETAPER_POLICY_STATE_DB` | `/var/lib/bytetaper/policy-state-db` |
| `BYTETAPER_CONTROL_PLANE_PORT` | `19090` |

### Runtime pod

| Variable | Example |
|----------|---------|
| `BYTETAPER_RUNTIME_ROLE` | `runtime-only` |
| `BYTETAPER_DEPLOYMENT_MODE` | `multi-runtime` |
| `BYTETAPER_CONTROL_PLANE_ENDPOINT` | `http://bytetaper-control-plane:19090` |
| `BYTETAPER_POLICY_PULL_ENABLED` | `1` |
| `BYTETAPER_POLICY_STATE_DIR` | `/var/lib/bytetaper/policies` |

Full profile: [docker-compose-profile.md](docker-compose-profile.md).

## Control Plane exposure

- Do not expose port `19090` publicly without [security.md](security.md) controls
- Place CP behind internal network or API gateway with auth
- Single-writer RocksDB — do not run multiple CP writers ([rocksdb-policy-state-store.md](rocksdb-policy-state-store.md))

## Related docs

- [security.md](security.md)
- [architecture-boundary.md](architecture-boundary.md)
- [production-readiness-checklist.md](production-readiness-checklist.md)

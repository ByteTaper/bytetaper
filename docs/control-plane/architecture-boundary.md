# ByteTaper Control Plane Architecture Boundary

## 1. Overview

Evolving ByteTaper from an Envoy-specific processor to a gateway-agnostic API Performance Runtime requires a clear boundary between policy configuration, lifecycle management, and request execution. The architecture boundary establishes a clean division of labor among the Control Plane, Runtime Policy Plane, Operational Plane, Data Plane Core, and Gateway Adapters. By decoupling policy ownership and translation logic, this boundary prevents runtime components from entangling themselves with gateway-specific APIs, avoids global state mutations inside transaction paths, and ensures that future gateway integrations can be implemented as lightweight adapters without duplicating core optimization and transformation semantics.

## 2. Five-Plane Model

The flow of policy configuration and runtime execution follows a strict hierarchical progression across five logical planes:

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
Gateway Adapters (Envoy / Nginx / HAProxy / Kong)
```

## 3. Plane Responsibilities Table

The table below delineates the strict boundaries of ownership and design constraints for each plane:

| Plane | Owns | Must Not |
|-------|------|----------|
| **Control Plane** | Global policy mutation, versioning, audit logging, and fleet status monitoring. | Serve live request traffic or depend on gateway-specific APIs (such as Envoy protobufs). |
| **Runtime Policy Plane** | Local snapshot activation, pulling/consuming committed policies from the state store, and initial YAML parsing/compilation. | Own global mutations or depend on any specific gateway adapter. |
| **Operational Plane** | Route epoch bump coordination, L1/L2 cache namespace cleanup, activation barriers, and readiness gating. | Decide or mutate the global active policy generation. |
| **Data Plane Core** | High-performance execution of cache lookup/store, field filtering, pagination, compression, and request coalescing. | Read policy YAML files on the hot path or directly contact the Control Plane. |
| **Gateway Adapters** | Translating gateway-specific request/response formats into internal ByteTaper envelopes, and routing traffic. | Own the policy parser/compiler or dictate cache invalidation semantics. |

## 4. Core Ownership Principle

The foundational relationships among the planes are defined by the following core principles:

```
Control Plane owns committed policy.
Runtime Policy Plane consumes committed policy.
Operational Plane makes policy safe to serve.
Data Plane Core uses immutable active snapshot.
Gateway Adapter translates traffic only.
```

## 5. Initial Deployment Model

To support deployment simplicity without compromising architectural boundaries, the initial deployment model is defined as follows:

* The **Control Plane** utilizes a dedicated, persistent `RocksDBPolicyStateStore` located at `/var/lib/bytetaper/policy-state-db` for immutable versioning and transaction audit tracking.
* This RocksDB policy state database instance is completely **separate** from the L2 response cache RocksDB database instance, ensuring storage namespace isolation.
* Runtime pods pull and consume committed policies from the shared state store but do not own the global write path or mutate configuration directly.
* To ensure startup resilience, runtime pods may maintain a local, last-known-good mirror of the compiled policy snapshot.
* Consensus systems (such as `etcd` or external SQL databases) and high-availability clustering are excluded from this initial deployment phase.

## 6. Proposed Module Boundary

To maintain source-level isolation, future code organization will align headers and implementations under the following directory layout:

```
include/
  control_plane/         — control_plane_service.h, policy_apply_api.h, policy_state_store.h, rocksdb_policy_state_store.h, fleet_status.h
  runtime_policy/        — runtime_policy_plane.h, runtime_policy_loader.h, runtime_policy_activation.h, runtime_policy_snapshot_consumer.h
  operational/           — policy_activation_barrier.h, cache_namespace_sync.h, route_epoch_sync.h, materialized_variant_sync.h
  gateway/               — gateway_request_envelope.h, gateway_response_envelope.h, gateway_adapter.h
  gateway_adapters/
    envoy/               — envoy_extproc_adapter.h
    nginx/               — nginx_adapter.h
    haproxy/             — haproxy_adapter.h
    kong/                — kong_adapter.h
```

## 7. Admin HTTP Surfaces

ByteTaper exposes two policy admin surfaces. Use the correct one for your deployment mode.

| Surface | Port (compose) | Paths | When to use |
|---------|----------------|-------|-------------|
| **Control Plane admin HTTP** | `19090` | `/admin/control-plane/*` | Multi-runtime, production: all committed-policy mutations and fleet status |
| **TaperQuery runtime admin HTTP** | `18082` (local only) | `/admin/taperquery/*` | Single-node local dev: in-process snapshot apply |

### Control Plane HTTP (implemented)

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/admin/control-plane/policy/current` | Active committed policy pointer |
| `GET` | `/admin/control-plane/policy/version?generation=N` | Immutable version fetch (includes `canonical_yaml`) |
| `POST` | `/admin/control-plane/policy/apply` | Submit async apply job (CAS-protected) |
| `GET` | `/admin/control-plane/fleet/status` | Fleet convergence and per-runtime status |
| `POST` | `/admin/control-plane/runtime/status` | Runtime heartbeat / status report |

Additional operations (`dry_run`, `diff`, `rollback`, `repair_local`, `adopt_local`) are available on the in-process `ControlPlaneService` API (integration tests and future HTTP exposure). See [api.md](api.md).

*Note: Single-node or local development may retain runtime-local TaperQuery apply shortcuts. Multi-runtime and production modes route committed-policy mutations through the Control Plane.*

## 8. Activation Barrier (Implemented)

**Persisted policy is not the same as operationally activated policy.** After the Control Plane commits a new generation, the Operational Plane runs an activation barrier before the Data Plane serves the new snapshot.

Stages (see [activation-barrier.md](activation-barrier.md)):

1. **Committed** — generation stored and active pointer promoted in `PolicyStateStore`.
2. **Operational diff computed** — route-level changes identified.
3. **Cache namespace prepared** — L1/L2 namespaces aligned with new generation.
4. **Route epochs bumped** — affected routes get a new cache epoch so old entries are unreachable.
5. **Cleanup enqueued** — L2 and materialized-variant cleanup may proceed asynchronously.
6. **Snapshot built and swapped** — new `RuntimePolicySnapshot` becomes the active serving snapshot.
7. **Activated** — readiness gate passes; data path uses the new generation.

Serving safety is immediate: old cache entries must not be reachable under the new policy generation even when background cleanup is still pending. See [cache-synchronization.md](cache-synchronization.md).

## 9. Non-Goals (Initial Release)

The following remain out of scope for the initial Control Plane release:

* Integration of `etcd` or external consensus mechanisms.
* High-availability clustering or replication for the Control Plane process.
* Runtime adapters for Nginx, HAProxy, or Kong (Envoy ext_proc is the initial gateway adapter).
* Kubernetes Operator, Helm chart, or Terraform modules.
* Distributed multi-writer `RocksDBPolicyStateStore` (single-writer only; see [rocksdb-policy-state-store.md](rocksdb-policy-state-store.md)).

## 10. Security and Deployment Guardrails

Control Plane mutation endpoints are guarded at the service boundary and at process startup:

* **Listener separation**: ext_proc gRPC (data path) on runtime pods, optional TaperQuery Admin HTTP on runtime (`127.0.0.1:18082`), and dedicated Control Plane admin HTTP (`19090`) for committed-policy mutation and fleet status (`/admin/control-plane/*`). See [docker-compose-profile.md](docker-compose-profile.md).
* **Deployment modes**: `local-dev`, `single-node`, `multi-runtime`, `production` — production enforces auth and rejects unsafe public bind without `allow_public_bind=true`.
* **Runtime-only role**: runtime pods pull committed policy; in-process mutation and admin apply are disabled.
* **Auth**: pluggable `ControlPlaneAuthProvider`; optional static bearer token via `BYTETAPER_CONTROL_PLANE_TOKEN` (never logged).
* **Dangerous operations**: repair, adopt, rollback, and bootstrap import when active policy exists require explicit confirmation flags.

## 11. Regression test matrix

Control Plane behavior is covered by tiered runners documented in [test-matrix.md](test-matrix.md):

| Tier | Proves |
|------|--------|
| `control_plane_unit` | PolicyStateStore, apply queue, guardrails, restart durability |
| `control_plane_integration` | Apply contract, pull loop, fleet/manual resolution, activation barrier |
| `control_plane_compose` | Docker profile: CP apply, fleet poll, Envoy data path, CP stop LKG, mirror tamper |

Commands: `make test-control-plane-unit`, `make test-control-plane-integration`, `make test-control-plane-compose`.

## 12. Future Extension Points

Storage mechanisms are designed behind abstract interfaces:
* The `PolicyStateStore` abstraction will support future backends such as etcd, relational databases (PostgreSQL), object storage (S3/GCS), or external managed control systems without modifying runtime loader logic.

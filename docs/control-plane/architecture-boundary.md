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

## 7. Mapping: Existing Admin HTTP → Future Control Plane

The table below maps the current behavior of the experimental TaperQuery Admin API to the formalized Control Plane design:

| Current Behavior | Future Control Plane Equivalent |
|------------------|----------------------------------|
| `GET /admin/taperquery/policy/current` | Control Plane active policy query |
| `POST /admin/taperquery/apply` | Control Plane apply endpoint |
| Dry-run configuration validation | Control Plane dry-run API |
| CAS (Compare-And-Swap) identity protection | Control Plane CAS check |
| Canonical YAML persistence | Control Plane immutable version store |
| Persistence failure prevents swap | Control Plane transactional commit |

*Note: Single-node or local development mode may retain runtime-local apply shortcuts. However, multi-pod production modes route all mutations strictly through the Control Plane.*

## 8. Activation Barrier (Conceptual)

The Operational Plane enforces a strict barrier ensuring that a new policy generation is only visible to the Data Plane once the following tasks are completed:

1. **Route Cache Epoch Bump**: All affected routes must increment their local cache epoch tag to logically isolate older cached entries.
2. **L1 Cache Namespace Cleanup**: L1 memory slots affected by configuration adjustments must be enqueued for removal or invalidated.
3. **L2 Cache Cleanup**: Background cleanup of disk-backed L2 caches for modified or deleted routes must be initiated or completed.
4. **Materialized Variant Invalidation**: Variant variants and indexes must be cleared.
5. **Readiness Gate**: The instance must pass health verification using the new snapshot.

This sequence represents a future requirement and is not implemented in the current runtime.

## 9. Non-Goals

The following items are explicitly out of scope for this architecture phase:
* Integration of `etcd` or external consensus mechanisms.
* Implementations of high-availability clustering or replication protocols for the Control Plane.
* Runtime adapters or implementations for Nginx, HAProxy, or Kong.
* Addition of new Admin HTTP endpoints.
* Integration or implementation of the RocksDB state store database.

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

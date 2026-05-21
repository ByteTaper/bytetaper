# Control Plane Security

## Overview

Control Plane mutations and sensitive operations are guarded at process startup and on each request. Security defaults favor safe local bind addresses, explicit production configuration, and optional static bearer authentication.

Implementation: [`control_plane_guardrails.cpp`](../../src/control_plane/control_plane_guardrails.cpp), tests: [`control_plane_security_test.cpp`](../../tests/control_plane_security_test.cpp).

## Listener separation

| Listener | Port (compose) | Exposure |
|----------|----------------|----------|
| ext_proc gRPC | runtime internal | Data path only |
| TaperQuery admin | `18082` | **Local dev only** — unauthenticated |
| Control Plane admin | `19090` | Committed policy + fleet — must be protected in production |

Never expose TaperQuery admin (`18082`) to untrusted networks. See [TaperQuery Admin HTTP](../taperquery/admin-http.md).

## Authentication

| Mode | Status |
|------|--------|
| `static-token` | Implemented — `BYTETAPER_CONTROL_PLANE_TOKEN` |
| `mtls` | Extension path (not initial release) |
| `oidc` | Extension path |
| `external` | Pluggable `ControlPlaneAuthProvider` |

Header: `Authorization: Bearer <token>`

### Token handling

- Tokens are **never** written to logs (verified by `AuthDenialSecurityLogOmitsBearerToken` test)
- Wrong token → `CONTROL_PLANE_UNAUTHORIZED` / auth denied metrics
- Production requires auth unless `allow_unauthenticated_production_mutation` explicitly set (unsafe)

## Bind address guardrails

| Setting | Behavior |
|---------|----------|
| Default | Prefer non-public bind |
| `0.0.0.0` / `::` | Requires `allow_public_bind=true` |
| Production + public bind without allow | **Startup failure** |

Environment: `BYTETAPER_CONTROL_PLANE_BIND_ADDRESS`, `allow_public_bind` in security config.

## Runtime-only role

`BYTETAPER_RUNTIME_ROLE=runtime-only`:

- In-process policy mutation rejected: `RUNTIME_ONLY_MUTATION_REJECTED`
- Must configure `BYTETAPER_CONTROL_PLANE_ENDPOINT` in `multi-runtime` and `production` modes

## Dangerous operations

Require explicit confirmation flags:

| Operation | Confirm flag |
|-----------|--------------|
| `repair_local` | `confirm=true` |
| `adopt_local` | `confirm_divergent_adoption=true` |
| `rollback` | `confirm=true` |
| Bootstrap import over active | Confirmation in bootstrap importer |

Missing confirm → `RejectedConfirmationRequired` / `*_CONFIRMATION_REQUIRED` error codes.

## Mutation guardrail error codes

| Code | Meaning |
|------|---------|
| `CONTROL_PLANE_UNAUTHORIZED` | Auth failed |
| `CONTROL_PLANE_MUTATION_DISABLED` | CP mutations disabled |
| `RUNTIME_ONLY_MUTATION_REJECTED` | Runtime attempted global mutation |

## Production checklist

- [ ] Static token or future mTLS/OIDC configured
- [ ] CP admin not on public Internet without fronting proxy + auth
- [ ] `allow_public_bind` only if intentional and firewalled
- [ ] Runtime role `runtime-only` in multi-pod deployments
- [ ] Secrets in env/K8s secrets, not in images

See [production-readiness-checklist.md](production-readiness-checklist.md) and [deployment-guardrails.md](deployment-guardrails.md).

## Future extensions

- mTLS between runtime and CP
- OIDC for operator apply
- RBAC per operation (apply vs rollback vs repair)

Document only — not implemented in initial release.

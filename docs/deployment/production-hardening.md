# Production Hardening Runbook

This guide defines the mandatory security, isolation, and reliability controls for running the ByteTaper API Performance Gateway in enterprise production environments.

## 1. Container Security & Identity

### Non-Root Execution
The ByteTaper container must run as an unprivileged user. The production Dockerfile and Helm charts enforce UID `1001` (`bytetaper`). Kubernetes PodSecurityStandards (Restricted) must be enforced across target namespaces.

### Read-Only Filesystem
Mount the container root filesystem as strictly read-only (`readOnlyRootFilesystem: true`). Ephemeral IPC socket communication (`/var/run/bytetaper`) and persistent caching (`/var/lib/bytetaper/l2-cache`) must be explicitly mounted into dedicated scratch volumes.

## 2. Pinned Immutable Deployments

Never deploy production instances using mutable tags (`latest`, `v0.1.0`). Deployments must reference the exact cryptographically signed SHA256 container image digest captured during the release publishing pipeline.

```yaml
image:
  repository: ghcr.io/bytetaper/bytetaper-runtime
  digest: "sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
```

## 3. Network Isolation & Control Plane Separation

### Admin Control Plane
The administrative control plane endpoint (port 18082) must remain disabled by default. If enabled for debugging, it must never be exposed via external LoadBalancers or Ingress routes. Restrict communication using Kubernetes NetworkPolicies to authorized internal tooling namespaces.

### NetworkPolicies
Enforce default-deny NetworkPolicies across the gateway namespace. Explicitly allow ingress traffic solely from authorized Envoy proxy instances on port 18080, and Prometheus scraping instances on port 18081.

## 4. Resource Allocation & Quality of Service

Configure explicit compute resource requests and limits to guarantee Guaranteed Quality of Service (QoS), preventing CPU throttling or Out-Of-Memory (OOM) evictions during traffic surges.

```yaml
resources:
  requests:
    cpu: "2000m"
    memory: "2Gi"
  limits:
    cpu: "4000m"
    memory: "4Gi"
```

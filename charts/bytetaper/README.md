# ByteTaper Helm Chart

Official Helm chart for deploying the hardened ByteTaper API Performance Gateway runtime on Kubernetes.

## Architecture & Container Contracts

This chart is built specifically around the hardened container contracts established in `ghcr.io/ByteTaper/bytetaper-runtime`:
* Writable mounts restricted strictly to `/var/lib/bytetaper/l2-cache` and `/var/run/bytetaper`.
* Total read-only root filesystem enforcement (`readOnlyRootFilesystem: true`).
* Execution pinned to non-root UID `1001` (`bytetaper`).
* All Linux capabilities dropped by default (`cap_drop: ALL`).

---

## Installation & Local Workflow

### 1. Local Kind Deployment Example
To spin up a local development or testing environment using `kind`:
```bash
kind create cluster --name bytetaper-chart
helm upgrade --install bytetaper ./charts/bytetaper \
  --values ./charts/bytetaper/ci/default-values.yaml \
  --namespace bytetaper --create-namespace
```

### 2. Uninstall
```bash
helm uninstall bytetaper --namespace bytetaper
```

---

## Configuration Modes

### 1. Image Tag Fallback vs. Digest Pinning
When `image.tag` and `image.digest` are omitted, the chart automatically falls back to `.Chart.AppVersion`. To guarantee absolute production immutability, supply the exact SHA256 digest:
```yaml
image:
  repository: ghcr.io/ByteTaper/bytetaper-runtime
  digest: "sha256:4c2759d9762b10723f1d4642f5419d6642131990b72f8bf30e68"
```

### 2. Policy Modes
The chart enforces exactly 3 mutually exclusive policy loading mechanisms:

#### A. Inline ConfigMap (Default)
Renders a ConfigMap managed by the release:
```yaml
policy:
  mode: inline
  inline: |
    routes:
      - match: { prefix: "/v1/data" }
```

#### B. Existing ConfigMap
References a pre-existing ConfigMap in the namespace and maps a configurable data key to `policy.yaml`:
```yaml
policy:
  mode: existingConfigMap
  existingConfigMap:
    name: "my-custom-policy-configmap"
    key: "custom-policy.yaml"
```

#### C. Existing Secret
References a pre-existing Kubernetes Secret in the namespace and maps a configurable data key to `policy.yaml`:
```yaml
policy:
  mode: existingSecret
  existingSecret:
    name: "my-secure-policy-secret"
    key: "secure-policy.yaml"
```

### 3. L2 Cache Storage Modes

#### A. PersistentVolumeClaim (Default)
Dynamically provisions a block storage PVC:
```yaml
l2Cache:
  persistence:
    enabled: true
    size: 20Gi
```

#### B. EmptyDir (Ephemeral)
For testing or ephemeral caching:
```yaml
l2Cache:
  persistence:
    enabled: false
  emptyDir:
    enabled: true
```

---

## Operational Probes & Metrics

Probes and Prometheus metrics are exposed out of the box on port `18081` (`metrics`):
* `/healthz`: Liveness probe endpoint.
* `/readyz`: Readiness probe endpoint.
* `/metrics`: Prometheus metric scraping endpoint.

To enable the automated Prometheus Operator `ServiceMonitor`:
```yaml
metrics:
  enabled: true
  serviceMonitor:
    enabled: true
    interval: 15s
```

---

## Admin Opt-In

By default, the TaperQuery administrative endpoint (`port 18082`) is fully disabled. To expose it:
```yaml
admin:
  enabled: true
  port: 18082
  service:
    enabled: true
    type: ClusterIP
```

---

## Production Values Reference

A fully verified production override example is maintained at `examples/values-production.yaml`. To deploy with production overrides:
```bash
helm upgrade --install bytetaper ./charts/bytetaper \
  --values ./charts/bytetaper/examples/values-production.yaml \
  --namespace bytetaper --create-namespace
```

---

## Troubleshooting & Operator Notes

### 1. Policy Mount Failures
* **Symptom:** Pod fails to start with `CreateContainerError` or `MountVolume.SetUp failed`.
* **Cause:** Configured `policy.existingConfigMap.name` or `key` does not exist in the namespace.
* **Fix:** Verify ConfigMap presence and key accuracy:
  ```bash
  kubectl get cm -n bytetaper my-custom-policy-configmap -o yaml
  ```

### 2. Image Pull Failures
* **Symptom:** Pod stuck in `ImagePullBackOff` or `ErrImagePull`.
* **Cause:** Missing private registry credentials or incorrect image digest.
* **Fix:** Supply registry pull secrets via `image.pullSecrets`.

### 3. PVC Provisioning Issues
* **Symptom:** Pod stuck in `Pending` state.
* **Cause:** Cluster lacks a default `StorageClass` or requested storage capacity is unavailable.
* **Fix:** Check PVC events and specify an explicit `storageClassName`.

### 4. Disabled Admin Endpoint Expectations
* **Symptom:** Connection refused on port `18082`.
* **Cause:** Admin endpoint is intentionally unexposed by default to prevent unauthorized query access.
* **Fix:** Explicitly toggle `admin.enabled: true`.

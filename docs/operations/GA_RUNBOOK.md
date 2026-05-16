# ByteTaper General Availability (GA) Operations Runbook

This runbook is the authoritative guide for operating ByteTaper in production environments. It covers installation, lifecycle management, performance tuning, and incident response.

---

## 1. Production Scope

ByteTaper is a high-performance API gateway and caching layer intended to run as an **Envoy External Processor (ExtProc)** service. 

*   **Release Integrity**: Production deployments MUST use images pinned by SHA256 digest from the official repository.
*   **Reference Architecture**: See `docs/deployment/helm-ha-production.md` for High Availability (HA) patterns.

## Supported Installation Paths

| Path | Recommended For | GA Status |
|---|---|---|
| Docker Compose | Single-node, appliance, smoke validation | Supported |
| Helm | Primary Kubernetes deployment | Supported |
| Terraform | IaC-managed Kubernetes deployment | Supported |
| Operator | Kubernetes-native lifecycle management | Advanced / optional |

---

## 2. Runtime Ports and Endpoints

| Port | Purpose | Production Exposure |
|---|---|---|
| **18080** | gRPC ExtProc Service | **Internal only**. Accessible only by Envoy. |
| **18081** | Metrics / healthz / readyz | **Internal**. Accessible by Prometheus/Kubernetes. |
| **18082** | TaperQuery Admin | **Restricted**. Disabled by default; use port-forward only. |

### Verification Commands
```bash
# Check liveness
curl -fsS http://localhost:18081/healthz

# Check readiness (L2 initialized and policy loaded)
curl -fsS http://localhost:18081/readyz

# Verify metrics
curl -fsS http://localhost:18081/metrics | grep bytetaper
```

---

## 3. Install via Docker Compose

### Deployment
1.  Initialize your environment file from the production template:
    ```bash
    cp docs/operations/examples/docker-compose-prod.env.example .env
    ```
2.  Set the target image digest in `.env`:
    ```bash
    BYTETAPER_IMAGE=ghcr.io/ByteTaper/bytetaper-runtime@sha256:<digest>
    ```
3.  Start the service:
    ```bash
    docker compose -f docker-compose.prod.yml up -d
    docker compose -f docker-compose.prod.yml ps
    docker compose -f docker-compose.prod.yml logs -f bytetaper
    ```

### Success Criteria
- Container status is `Up (healthy)`.
- `readyz` endpoint returns 200 OK.
- `metrics` endpoint is reachable and returns ByteTaper metrics.
- L2 cache directory is writable by the `bytetaper` user (UID 1001).
- Policy file is mounted read-only and loaded successfully (check logs).
- Admin endpoint is disabled unless explicitly enabled via override.

### Shutdown
```bash
# Stop and remove containers
docker compose -f docker-compose.prod.yml down

# Full cleanup (Warning: DELETES L2 CACHE VOLUME)
docker compose -f docker-compose.prod.yml down -v
```

---

## 4. Install via Helm

### Deployment
```bash
helm upgrade --install bytetaper charts/bytetaper \
  --set image.digest="sha256:<digest>" \
  --values docs/operations/examples/helm-prod-values.yaml \
  --namespace bytetaper --create-namespace
```

> [!WARNING]
> **Policy Required**: The example values render with an empty route list (`routes: []`). Operators MUST supply a real policy via a custom values file, ConfigMap, or Secret before the gateway can process traffic.

### Production Choices
When deploying via Helm, ensure the following are configured:
- **Image**: Pinned by top-level manifest-list digest.
- **Policy**: Source (inline, ConfigMap, or Secret) defined and verified.
- **L2 Storage**: Persistence enabled for single-replica; `emptyDir` for multi-replica HA.
- **Admin**: Disabled (`admin.enabled: false`) for public exposure.
- **Observability**: Metrics and probes (Liveness/Readiness) enabled.
- **Resilience**: Resource requests/limits and PodDisruptionBudget (PDB) active.

### Verification
```bash
kubectl rollout status deployment/bytetaper -n bytetaper
kubectl get pods -n bytetaper
kubectl get svc -n bytetaper

# Verify health and metrics via port-forward
kubectl port-forward svc/bytetaper 18081:18081 -n bytetaper
curl -fsS http://localhost:18081/readyz
curl -fsS http://localhost:18081/metrics | grep bytetaper
```

---

## 5. Upgrade Procedure

### Docker Compose
1.  Update `BYTETAPER_IMAGE` digest in `.env`.
2.  Pull new image: `docker compose -f docker-compose.prod.yml pull`.
3.  Restart service: `docker compose -f docker-compose.prod.yml up -d`.
4.  **Verify Upgrade**:
    - `docker compose -f docker-compose.prod.yml ps` (Status should be healthy)
    - `curl -fsS http://localhost:18081/readyz` (200 OK)
    - `docker compose -f docker-compose.prod.yml logs --tail=200` (No startup errors)
    - Check version: `docker compose -f docker-compose.prod.yml exec bytetaper bytetaper-extproc-server --version`
    - `curl -fsS http://localhost:18081/metrics | grep bytetaper`

### Helm
1.  Run `helm upgrade` with the new image digest:
    ```bash
    helm upgrade bytetaper charts/bytetaper \
      --set image.digest="sha256:<new-digest>" \
      --reuse-values \
      -n bytetaper
    ```
2.  **Verify Rollout**:
    - `kubectl rollout status deployment/bytetaper -n bytetaper`
    - `kubectl -n bytetaper get deploy bytetaper -o jsonpath='{.spec.template.spec.containers[0].image}'`
    - `kubectl port-forward svc/bytetaper 18081:18081 -n bytetaper`
    - `curl -fsS http://localhost:18081/readyz` (200 OK)
    - `curl -fsS http://localhost:18081/metrics | grep bytetaper`

---

## 6. Rollback Procedure

### Docker Compose
1.  Revert `BYTETAPER_IMAGE` to the previous known-good digest in `.env`.
2.  Restart: `docker compose -f docker-compose.prod.yml up -d`.
3.  **Verify Recovery**:
    - `docker compose -f docker-compose.prod.yml ps` (Status should be healthy)
    - `curl -fsS http://localhost:18081/readyz` (200 OK)
    - `docker compose -f docker-compose.prod.yml logs --tail=200 bytetaper` (Verify policy reload)

### Helm
1.  Identify the last good revision: `helm history bytetaper -n bytetaper`.
2.  Rollback: `helm rollback bytetaper <revision-number> -n bytetaper`.
3.  **Verify Recovery**:
    - `kubectl rollout status deployment/bytetaper -n bytetaper`
    - Check logs for policy reload: `kubectl logs deployment/bytetaper -n bytetaper --tail=100`

---

## 7. Backup L2 Cache

The L2 RocksDB cache should be backed up during maintenance windows if persistence of warm data is critical.

### Docker Compose
```bash
docker compose -f docker-compose.prod.yml stop
# Backup the named volume to a tarball
docker run --rm -v bytetaper_bytetaper-l2-cache:/data -v $(pwd):/backup alpine tar czf /backup/l2-cache-backup.tar.gz -C /data .
docker compose -f docker-compose.prod.yml start
```

### Kubernetes
- **Preferred**: Use a `VolumeSnapshot` if your StorageClass supports CSI snapshots.
- **Manual**: Scale the deployment to 0, create a temporary pod to tar the PVC contents, and scale back up.

---

## 8. Delete or Rebuild L2 Cache

Delete the L2 cache if you suspect data corruption or if you need a "cold start" for performance benchmarking.

### Impact
- **Warning**: Deleting the L2 cache results in a "cold" gateway. Upstream traffic and latency will increase until the cache re-warms.

### Docker Compose
```bash
docker compose -f docker-compose.prod.yml down -v
docker compose -f docker-compose.prod.yml up -d
```

### Kubernetes
```bash
kubectl scale deployment bytetaper --replicas=0 -n bytetaper
kubectl delete pvc bytetaper-l2-cache -n bytetaper
kubectl scale deployment bytetaper --replicas=1 -n bytetaper
```

---

## 9. Tuning RocksDB and Worker Settings

Tune performance via environment variables in `.env` (Docker Compose) or Helm `env`.

### L2 (RocksDB) Tuning
| Variable | Default | Purpose |
|---|---|---|
| `BYTETAPER_L2_BLOCK_CACHE_MB` | 64 | LRU block cache size. Increase for high hit rates. |
| `BYTETAPER_L2_WRITE_BUFFER_MB` | 32 | Size of the write memtable. |
| `BYTETAPER_L2_MAX_BACKGROUND_JOBS` | 2 | Concurrent flush/compaction jobs. |

### Worker Tuning
| Variable | Default | Purpose |
|---|---|---|
| `BYTETAPER_WORKER_COUNT` | 2 | Threads for background IO and coalescing. |
| `BYTETAPER_LOOKUP_LANE_QUOTA` | 4 | Max concurrent lookups per scheduling cycle. |
| `BYTETAPER_STORE_LANE_QUOTA` | 1 | Max concurrent stores per scheduling cycle. |
| `BYTETAPER_ASYNC_STORE_MAX_BODY_SIZE` | derived | Max body size (bytes) to store in L2 cache. When unset, derived from route `max_response_bytes`, capped at 1 MiB. See `docs/runtime/CONFIGURATION.md`. |

### Tuning Profiles
- **Read-Heavy**: Increase `BYTETAPER_L2_BLOCK_CACHE_MB` (e.g., 512+).
- **Coalescing-Heavy**: Increase `BYTETAPER_WORKER_COUNT` (e.g., 4-8).

### Validation
Verify effective values via metrics:
```bash
curl -fsS http://localhost:18081/metrics | grep _effective
# Example output:
# bytetaper_worker_count_effective 2
# bytetaper_worker_async_store_max_body_size_effective 1048576
# bytetaper_l2_block_cache_mb_effective 64
```

---

## 10. Enable or Disable Admin Endpoint

The TaperQuery admin endpoint (`port 18082`) should only be enabled for troubleshooting.

### Enable (Docker Compose)
Admin enablement in Docker Compose requires overriding the service command to include the admin flags. Create a `docker-compose.override.yml` that preserves the base arguments:
```yaml
services:
  bytetaper:
    command:
      - "--listen-address"
      - "0.0.0.0:18080"
      - "--policy-file"
      - "/etc/bytetaper/policy.yaml"
      - "--l2-cache-path"
      - "/var/lib/bytetaper/l2-cache"
      - "--metrics-address"
      - "0.0.0.0"
      - "--metrics-port"
      - "18081"
      - "--admin-enable-taperquery"
      - "--admin-address"
      - "0.0.0.0"
      - "--admin-port"
      - "18082"
```

### Disable (Docker Compose)
To disable the admin endpoint, remove the `docker-compose.override.yml` file and recreate the service:
```bash
rm docker-compose.override.yml
docker compose -f docker-compose.prod.yml up -d --force-recreate
```

### Enable (Helm)
```bash
helm upgrade bytetaper charts/bytetaper \
  --set admin.enabled=true \
  --set admin.address=127.0.0.1 \
  --set admin.service.enabled=false \
  --reuse-values \
  -n bytetaper
# ACCESS ONLY VIA PORT-FORWARD (deploy, not svc, since service is disabled):
kubectl -n bytetaper port-forward deploy/bytetaper 18082:18082
```

### Disable (Helm)
```bash
helm upgrade bytetaper charts/bytetaper \
  --set admin.enabled=false \
  --set admin.service.enabled=false \
  --reuse-values \
  -n bytetaper
```

---

## 11. Incident Guide — Latency Spike

### Symptoms
- Increased p95/p99 latency in Envoy metrics.
- `bytetaper_cache_l1_hit_total` or `bytetaper_cache_l2_hit_total` ratio decreases.
- `bytetaper_runtime_worker_queue_depth` increases.

### Checks
1.  Check upstream service health and latency.
2.  Check for readiness probe failures or restart loops.
3.  Monitor `kubectl top pods` or `docker stats` for CPU/Memory saturation.

### Mitigations
- **Immediate**: Increase `BYTETAPER_WORKER_COUNT` or `BYTETAPER_LOOKUP_LANE_QUOTA`.
- **Long-term**: Increase L2 block cache size or scale out replicas (ensuring Consistent Hashing is used in Envoy).

---

## 12. Incident Guide — Cache Corruption

### Symptoms
- Pods stuck in `CrashLoopBackOff`.
- Logs show `RocksDB: Corruption` or `IO error: lock file`.
- `readyz` endpoint fails to initialize.

### Mitigation
1.  **Stop** the service.
2.  **Delete** the L2 cache directory contents (see Section 8).
3.  **Restart** the service. A cold start is safer than serving corrupted data.

---

## 13. Incident Guide — Invalid Policy

### Symptoms
- 500 errors from ByteTaper after a policy update.
- Logs show `Failed to load policy: <yaml error>`.

### Mitigation
1.  Revert the Policy ConfigMap/Secret to the last known-good version.
2.  Restart the pods to force a reload.
3.  Use the `bytetaper-validate-policy` tool in your CI/CD pipeline to prevent future invalid updates.

---

## 14. Incident Guide — High Memory (OOM)

### Symptoms
- Pod status `OOMKilled`.
- Memory usage metrics reaching the limit.

### Mitigation
1.  Verify if `BYTETAPER_L2_BLOCK_CACHE_MB` plus `BYTETAPER_L2_WRITE_BUFFER_MB` exceeds the container memory request/limit.
2.  **Reduce** cache memory settings or **Increase** the container memory limit.
3.  If OOM persists, check logs for large request/response body processing issues and consider reducing max body size limits in policy.

---

## 15. Production Checklist

- [ ] **Image Pinned**: Top-level manifest-list digest used in `image.digest`.
- [ ] **Probes Active**: Liveness and Readiness probes returning 200 OK.
- [ ] **Metrics Scraped**: `bytetaper_` metrics visible in Prometheus.
- [ ] **Admin Secured**: `admin.enabled` is `false` or restricted via NetworkPolicy.
- [ ] **L2 Persistent**: Independent PVC (RWO) confirmed for single-replica mode.
- [ ] **Resources Set**: CPU/Memory requests and limits match the workload profile.
- [ ] **Logging Active**: Pod logs are being shipped to a central collector.
- [ ] **Backup Verified**: L2 backup/restore procedure has been tested.
- [ ] **Incident Escalation**: On-call engineer knows how to access ByteTaper metrics.
- [ ] **If using Terraform**: `terraform plan` reviewed before apply; image digest and chart version pinned.
- [ ] **If using Operator**: CRD version and controller image pinned; custom resource status conditions healthy.
- [ ] **If using Operator**: PVC deletion behavior confirmed before any production deletion.

---

## 16. Install via Terraform

### 16.1 Overview

> Terraform is the production infrastructure-as-code path for installing ByteTaper in Kubernetes environments. The Terraform module deploys the ByteTaper Helm chart with explicit production values.

Module path: `terraform/modules/bytetaper-helm`

Available examples: `terraform/examples/{kind,eks,gke,aks}`

### 16.2 Required Inputs Table

| Input | Purpose |
|---|---|
| `namespace` | Kubernetes namespace |
| `release_name` | Helm release name |
| `chart_version` | Helm chart version (e.g., `1.0.0`) |
| `chart_path` | Local filesystem path to chart (optional) |
| `image_repository` | Runtime image repository |
| `image_digest` | Digest-pinned runtime image |
| `policy_mode` | inline / existingConfigMap / existingSecret |
| `l2_cache_enabled` | Enable RocksDB L2 cache (also enables PVC persistence in this module) |
| `l2_cache_size` | PVC size |
| `l2_cache_storage_class_name` | StorageClass for L2 PVC |
| `admin_enabled` | TaperQuery admin (keep false) |
| `metrics_enabled` | Metrics endpoint |
| `resources` | CPU/memory requests and limits |
| `extra_values` | Map of additional Helm values (e.g., `replicaCount`) |

### 16.3 Production Example

```hcl
module "bytetaper" {
  source = "../../modules/bytetaper-helm"

  namespace    = "bytetaper"
  release_name = "bytetaper"

  image_repository = "ghcr.io/ByteTaper/bytetaper-runtime"
  image_digest     = "sha256:REPLACE_WITH_RELEASE_DIGEST"

  chart_version = "1.0.0"

  extra_values = {
    replicaCount = 1
  }

  l2_cache_enabled = true
  l2_cache_size    = "20Gi"

  admin_enabled   = false
  metrics_enabled = true

  resources = {
    requests = { cpu = "500m", memory = "512Mi" }
    limits   = { cpu = "2000m", memory = "2Gi" }
  }
}
```

### 16.4 Local Validation

```bash
# Verify formatting
terraform fmt -check -recursive terraform/

# Validate module configuration
terraform -chdir=terraform/modules/bytetaper-helm init -backend=false
terraform -chdir=terraform/modules/bytetaper-helm validate
```

### 16.5 Install Flow

```bash
terraform -chdir=terraform/examples/eks init
terraform -chdir=terraform/examples/eks plan
terraform -chdir=terraform/examples/eks apply
kubectl -n bytetaper rollout status deployment/bytetaper
kubectl -n bytetaper get all
kubectl -n bytetaper get pvc
kubectl -n bytetaper port-forward svc/bytetaper 18081:18081
curl -fsS http://localhost:18081/healthz
curl -fsS http://localhost:18081/readyz
curl -fsS http://localhost:18081/metrics | grep bytetaper
```

### 16.6 Upgrade

1. Update `image_digest` or `chart_version` in your module configuration or `.tfvars`.
2. Review the plan:
   ```bash
   terraform -chdir=terraform/examples/eks plan
   ```
3. Apply the changes:
   ```bash
   terraform -chdir=terraform/examples/eks apply
   ```
4. Verify rollout and health:
   ```bash
   kubectl -n bytetaper rollout status deployment/bytetaper
   kubectl -n bytetaper get pods
   curl -fsS http://localhost:18081/readyz
   ```

### 16.7 Rollback

1. Revert `image_digest` or `chart_version` to the previous known-good values.
2. Review the plan:
   ```bash
   terraform -chdir=terraform/examples/eks plan
   ```
3. Apply the rollback:
   ```bash
   terraform -chdir=terraform/examples/eks apply
   ```
4. Verify restoration:
   ```bash
   kubectl -n bytetaper rollout status deployment/bytetaper
   kubectl -n bytetaper logs deploy/bytetaper --tail=100
   curl -fsS http://localhost:18081/readyz
   ```

### 16.7 Destroy Warning

> [!CAUTION]
> Do not assume `terraform destroy` should delete production L2 cache data. Back up or snapshot the PVC before running destroy if warm cache state matters.

```bash
kubectl -n bytetaper get pvc  # check before destroy
terraform -chdir=terraform/examples/eks destroy
```

### 16.8 Troubleshooting Table

| Symptom | Cause | Action |
|---|---|---|
| `terraform validate` fails | Module input mismatch | Check module variables |
| Helm release fails | Invalid chart values | `helm template` with equivalent values |
| Pods pending | PVC/storage class issue | Check PVC events, storage class |
| Readiness fails | Invalid policy or L2 path | Check logs, ConfigMap/Secret, PVC mount |
| Admin unreachable | Disabled by default | Confirm `admin_enabled` and service exposure |

```bash
terraform -chdir=terraform/examples/eks state list
kubectl -n bytetaper describe pod <pod>
kubectl -n bytetaper get events --sort-by=.lastTimestamp
kubectl -n bytetaper logs deploy/bytetaper --tail=300
```

---

## 17. Install via Kubernetes Operator (Advanced / Optional)

### 17.1 Overview

> The Kubernetes Operator provides a Kubernetes-native lifecycle management path. Helm remains the primary Kubernetes GA install path. Use the Operator for teams that want reconciliation-based management and plan to adopt future automation features.

### 17.2 Install CRDs and Controller

```bash
kubectl apply -f operator/config/crd/bases/
kubectl apply -f operator/config/manager/
kubectl get pods -n bytetaper-system
kubectl logs -n bytetaper-system deploy/bytetaper-operator-controller-manager --tail=200
```

### 17.3 Apply Custom Resource

```bash
kubectl apply -f docs/operations/examples/operator-bytetaper-sample.yaml
kubectl get bytetapergateways -A
kubectl describe bytetapergateway bytetaper -n bytetaper
```

### 17.4 Example CR

```yaml
apiVersion: bytetaper.io/v1alpha1
kind: ByteTaperGateway
metadata:
  name: bytetaper
  namespace: bytetaper
spec:
  image:
    repository: ghcr.io/ByteTaper/bytetaper-runtime
    digest: sha256:REPLACE_WITH_RELEASE_DIGEST
  replicas: 1
  policy:
    configMapRef:
      name: bytetaper-policy
    key: policy.yaml
  l2Cache:
    enabled: true
    persistence:
      enabled: true
      size: 20Gi
  admin:
    enabled: false
  metrics:
    enabled: true
  resources:
    requests: { cpu: 500m, memory: 512Mi }
    limits: { cpu: "2", memory: 2Gi }
```

### 17.5 Runtime Verification

```bash
kubectl -n bytetaper rollout status deployment/bytetaper
kubectl -n bytetaper get deploy,svc,pvc,cm,hpa
kubectl -n bytetaper port-forward svc/bytetaper 18081:18081
curl -fsS http://localhost:18081/healthz
curl -fsS http://localhost:18081/readyz
curl -fsS http://localhost:18081/metrics | grep bytetaper
```

### 17.6 Upgrade (Runtime)

1. Edit `spec.image.digest` in your `ByteTaperGateway` manifest.
2. Apply the change:
   ```bash
   kubectl apply -f docs/operations/examples/operator-bytetaper-sample.yaml
   ```
3. Verify rollout:
   ```bash
   kubectl -n bytetaper rollout status deployment/bytetaper
   kubectl -n bytetaper get pods
   kubectl -n bytetaper logs deploy/bytetaper --tail=100
   ```

### 17.7 Upgrade (Operator Controller)

```bash
kubectl apply -f operator/config/manager/
kubectl -n bytetaper-system rollout status deployment/bytetaper-operator-controller-manager
```

### 17.8 Rollback

1. Revert `spec.image.digest` to the previous known-good digest in the manifest.
2. Apply the rollback:
   ```bash
   kubectl apply -f docs/operations/examples/operator-bytetaper-sample.yaml
   ```
3. Verify recovery:
   ```bash
   kubectl -n bytetaper rollout status deployment/bytetaper
   kubectl -n bytetaper get pods
   kubectl -n bytetaper logs deploy/bytetaper --tail=100
   ```

### 17.9 Deletion Warning

> [!CAUTION]
> Before deleting a `ByteTaperGateway` custom resource in production, confirm whether the Operator deletes PVCs or preserves them. Back up or snapshot the PVC if cache data matters.

```bash
kubectl delete bytetapergateway bytetaper -n bytetaper
kubectl -n bytetaper get deploy,svc,pvc,cm  # verify retention behavior
```

### 17.10 Troubleshooting Table

| Symptom | Cause | Action |
|---|---|---|
| CRD not recognized | CRD not applied | Apply CRDs, check `kubectl api-resources` |
| Reconciliation stuck | Invalid CR spec | `kubectl describe bytetapergateway`, check controller logs |
| Deployment not created | RBAC issue | Check controller manager logs and permissions |
| Pod not ready | Policy/cache/config issue | Check generated Deployment, ConfigMap/Secret, PVC |
| PVC stuck pending | StorageClass/Quota issue | `kubectl get pvc`, `kubectl describe pvc`, check Events |
| Admin exposed unexpectedly | Spec misconfiguration | Check generated Service and NetworkPolicy |

```bash
kubectl api-resources | grep -i bytetaper
kubectl get bytetapergateways -A
kubectl logs -n bytetaper-system deploy/bytetaper-operator-controller-manager --tail=300
kubectl -n bytetaper get events --sort-by=.lastTimestamp
```

### 17.11 Production Safety Notes

- **Status Conditions**: Verify that the `ByteTaperGateway` resource reports healthy conditions:
  - `Ready`: True (Overall operational status)
  - `PolicyReferenced`: True (ConfigMap/Secret reference is valid)
  - `PolicyMounted`: True (Config successfully mounted to pods)
  - `CachePersistent`: True (PVC bound and persistent cache ready)
  - `DeploymentReady`: True (Underlying Deployment is at desired replicas)
  - `ServiceReady`: True (Core Service is created and valid)
  - `AdminExposed`: Match spec (Admin endpoint exposure status)
- **Metrics Readiness**: Dedicated status conditions for metrics are not yet implemented. Verify metrics by accessing the `/metrics` endpoint on the pods directly or via the service.
- **Reconciliation Errors**: Inspect the `status.conditions` for `Ready: False`. For detailed error messages, check:
  - `kubectl describe bytetapergateway bytetaper -n bytetaper` (look at Events)
  - `kubectl logs -n bytetaper-system deploy/bytetaper-operator-controller-manager --tail=200`
- CRD version compatibility must be confirmed before GA.
- Operator upgrades must be tested in staging before production.
- Admin endpoint must stay disabled by default in CR spec.
- PVC deletion behavior must be documented and understood before production use.
- Helm remains the safer default until Operator lifecycle is fully validated.

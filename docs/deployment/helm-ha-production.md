# ByteTaper Helm HA Production Guide

This guide details the recommended High Availability (HA) deployment models for ByteTaper on Kubernetes, explaining the relationship between pod replicas and the tiered caching system.

## 1. Cache Model & Architecture

ByteTaper uses a tiered caching strategy that is tightly coupled to the runtime process:

*   **L1 Cache**: In-process memory cache. Unique to each pod replica.
*   **L2 Cache**: Local RocksDB disk cache. Unique to each pod replica.

### The No-Shared-Cache Rule
The L2 RocksDB cache is **NOT** shared across replicas. You must **NEVER** mount the same ReadWriteMany (RWX) volume or the same PersistentVolume (PV) path into multiple ByteTaper pods simultaneously. Doing so will lead to RocksDB database corruption and runtime crashes.

## 2. Recommended Deployment Models

### A. Single-Replica Production (Safe Persistence)
Recommended for initial production rollouts where cache warmness across restarts is critical.

*   **Replica Count**: 1
*   **Storage**: PersistentVolumeClaim (RWO) for L2 cache.
*   **Pros**: 100% cache warmness retention across pod restarts.
*   **Cons**: Brief downtime during rolling updates.

**Values Example**: `charts/bytetaper/examples/single-replica-prod-values.yaml`

### B. Multi-Replica HA (Safe Performance)
Recommended for high-traffic environments requiring zero-downtime updates.

*   **Replica Count**: 3+
*   **Storage**: **emptyDir** (Required for safe multi-replica Deployment).
*   **Pros**: Zero-downtime upgrades; distributed load; resilience to node failure.
*   **Cons**: Cache is lost on pod restart; new replicas start cold.

**Values Example**: `charts/bytetaper/examples/multi-replica-ha-values.yaml`

> [!IMPORTANT]
> **Deployment vs. StatefulSet**: The current ByteTaper Helm chart uses a `Deployment`. In Kubernetes, a Deployment with multiple replicas and a PVC will attempt to mount the *same* PVC into all pods, which violates the No-Shared-Cache rule. **Persistent multi-replica HA is a future roadmap item requiring a StatefulSet implementation.**

## 3. Envoy Load Balancing & Locality

The effectiveness of the ByteTaper cache depends on request locality. If the Envoy `ext_proc` filter is configured with simple Round Robin load balancing across 3 ByteTaper replicas, each replica will only see ~33% of the traffic, leading to slower cache warmup and lower aggregate hit rates.

*   **Recommendation**: Use **Consistent Hashing** or **Maglev** load balancing in your Envoy Cluster configuration (targeting the ByteTaper service) to ensure the same upstream requests are routed to the same ByteTaper pod whenever possible.

## 4. Admin Endpoint & Security

The TaperQuery admin endpoint (`port 18082`) provides deep visibility into cache internals and should be strictly secured in production.

*   **Rule**: Keep `admin.enabled: false` by default.
*   **NetworkPolicy**: If enabled, always apply a `NetworkPolicy` to restrict ingress to authorized pods (e.g., your Prometheus scraper or an internal admin bastion).

**Values Example**: `charts/bytetaper/examples/network-policy-values.yaml`

## 5. Upgrades & Rollbacks

ByteTaper uses a `RollingUpdate` strategy.

*   **Readiness Probes**: The chart includes readiness probes that gate traffic until the gateway is initialized.
*   **Cold-Cache Impact**: During a rolling update of an `emptyDir` HA deployment, new pods start with empty caches. Aggregate upstream load may spike during the update window as caches re-warm.
*   **Scaling Down**: Scaling down removes a portion of the warmed cache capacity. If traffic is high, ensure the remaining replicas have sufficient L2 disk space and CPU to handle the additional load and potential re-caching.
*   **Rollback**: Use `helm rollback` to return to a known-good image version or policy configuration.

## 6. Horizontal Pod Autoscaling (HPA)

While the ByteTaper chart supports HPA (`autoscaling.enabled: true`), it should be used conservatively in production:

*   **Warmup Penalty**: New replicas started by HPA begin with empty L1/L2 caches. This temporarily increases the load on upstream services as the new pod warms up.
*   **Safe Controls**: 
    - Set a conservative `maxReplicas` to avoid overwhelming upstream during extreme bursts.
    - Use a high `targetCPUUtilizationPercentage` (e.g., 75%+) to ensure scaling only occurs during sustained load.
*   **Recommendation**: Keep HPA disabled (`autoscaling.enabled: false`) for initial production deployments until you have measured the impact of cache warmup on your upstream services.

## 7. Storage Class Guidance (Single-Replica)

When using persistent L2 caching in single-replica mode, your `StorageClass` choice impacts performance and reliability:

*   **Access Mode**: Use `ReadWriteOnce` (RWO). Avoid `ReadWriteMany` (RWX) for RocksDB.
*   **Latency**: Pick a storage class with predictable, low-latency IOPS (e.g., SSD-backed volumes) to ensure RocksDB performance remains high during heavy cache churn.
*   **Attach/Detach Reliability**: Verify your cloud provider's behavior during pod rescheduling. Long PVC detach/attach times can increase downtime during rolling updates.
*   **Backup**: Ensure your `StorageClass` supports volume snapshots if you intend to back up the L2 cache state.

## 8. Production Checklist

- [ ] **Image Pinned**: Use absolute SHA256 digests in `image.digest`.
- [ ] **Admin Disabled**: Keep the TaperQuery admin endpoint disabled unless required.
- [ ] **Resources Configured**: Set explicit CPU/Memory requests and limits.
- [ ] **PDB Active**: Ensure `podDisruptionBudget.enabled: true`.
- [ ] **Probes Verified**: Ensure Liveness and Readiness probes are active.
- [ ] **Safe Storage**: Use PVC for 1-replica, or `emptyDir` for multi-replica.
- [ ] **Topology Spread**: Ensure replicas are spread across nodes.

## 9. Troubleshooting

| Symptom | Probable Cause | Fix |
| :--- | :--- | :--- |
| RocksDB Lock Error | `replicaCount > 1` with `persistence.enabled` | Reduce to 1 replica or switch to `emptyDir`. |
| Low Hit Rate | Round-robin LB across replicas | Switch Envoy LB policy to Consistent Hashing. |
| Pods stuck in Pending | PVC binding failure | Verify `StorageClass` or check if PVC is already attached to another node. |
| 503 during upgrade | Readiness probe timeout | Increase `initialDelaySeconds` or `failureThreshold` if initialization is slow. |
| High Upstream Load | HPA scaling cold replicas | Cap `maxReplicas` conservatively and increase target CPU/memory thresholds. |
| PVC Mount Delay | Slow volume detach/attach | Investigate `StorageClass` or provisioner latency; ensure pod is not stuck in `ContainerCreating`. |

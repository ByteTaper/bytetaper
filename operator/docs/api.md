# ByteTaperGateway CRD API Reference

The `ByteTaperGateway` Custom Resource Definition (`bytetaper.io/v1alpha1`) provides declarative, Kubernetes-native reconciliation for the ByteTaper high-performance Envoy external processor.

## Spec Fields (`spec`)

| Field | Type | Default | Description |
|---|---|---|---|
| `replicas` | int32 | `1` | Desired number of gateway pods. |
| `image.repository` | string | `ghcr.io/bytetaper/bytetaper-runtime` | Container image repository. |
| `image.tag` | string | `latest` | Container image tag. |
| `image.digest` | string | `""` | SHA256 digest pinning. Overrides tag. |
| `image.pullPolicy` | string | `IfNotPresent` | Image pull policy (`Always`, `IfNotPresent`, `Never`). |
| `policy.configMapRef` | object | `nil` | Local reference to a ConfigMap containing `policy.yaml`. |
| `policy.secretRef` | object | `nil` | Local reference to a Secret containing `policy.yaml`. |
| `policy.key` | string | `policy.yaml` | Key within ConfigMap or Secret containing policy contents. |
| `l2Cache.enabled` | bool | `true` | Enable L2 cache mounting. |
| `l2Cache.persistence.enabled` | bool | `true` | Enable automated PVC provisioning. |
| `l2Cache.persistence.size` | string | `10Gi` | Persistent volume claim storage request size. |
| `l2Cache.persistence.storageClassName` | string | `""` | Storage class to request. |
| `l2Cache.persistence.existingClaim` | string | `""` | Reference an existing PVC to mount. |
| `admin.enabled` | bool | `false` | Enable TaperQuery administrative endpoint. |
| `admin.serviceEnabled` | bool | `false` | Expose admin endpoint via a Kubernetes ClusterIP Service. |
| `resources` | object | `{}` | Container resource requests and limits. |

## Status Conditions (`status.conditions`)

| Condition | Description |
|---|---|
| `Ready` | Gateway is operational, policy is referenced, and replicas are available. |
| `PolicyReferenced` | ConfigMap or Secret referenced in spec exists in the namespace. |
| `DeploymentReady` | Managed Deployment has at least one ready replica. |
| `CachePersistent` | Associated PVC is successfully Bound. |
| `AdminExposed` | Admin service is provisioned and active. |

> [!WARNING]
> Admin endpoints provide privileged diagnostic queries and mutators. Ensure administrative services are protected or restricted via NetworkPolicies when enabled.

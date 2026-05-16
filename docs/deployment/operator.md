# ByteTaper Operator Deployment Runbook

This runbook guides platform engineers through deploying and managing ByteTaper via the official Kubernetes Operator (`BT-DIST-004`).

## 1. When to Use Operator vs Helm

While the Helm chart (`charts/bytetaper`) remains the canonical deployment package for standard CI/CD pipelines, the Operator provides continuous self-healing, automated PVC claim binding checks, and real-time operational status conditions.

## 2. Installation Architecture

1. Apply Custom Resource Definitions:
   ```bash
   kubectl apply -f operator/config/crd/bases
   ```
2. Deploy Controller Manager into bytetaper-operator-system namespace:
   ```bash
   kubectl apply -k operator/config/default
   ```

## 3. Policy & Digest Pinning Configuration

The operator expects policy configurations decoupled via ConfigMaps or Secrets. To enforce supply-chain security in production, always provide pinned SHA256 image digests:

```yaml
apiVersion: bytetaper.io/v1alpha1
kind: ByteTaperGateway
metadata:
  name: production-gateway
  namespace: bytetaper
spec:
  replicas: 3
  image:
    repository: ghcr.io/bytetaper/bytetaper-runtime
    digest: sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
  policy:
    configMapRef:
      name: production-policy
  l2Cache:
    enabled: true
    persistence:
      enabled: true
      storageClassName: gp3
```

## 4. Troubleshooting & Status Conditions

Monitor gateway health directly through CRD status conditions:

```bash
kubectl get bytetapergateway production-gateway -n bytetaper -o jsonpath='{.status.conditions}'
```

Look for `Ready=True`. If `PolicyReferenced=False`, verify that the referenced ConfigMap or Secret exists in the matching namespace.

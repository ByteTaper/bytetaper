# ByteTaper Kubernetes Operator

The ByteTaper Operator automates the deployment, reconciliation, and lifecycle management of the ByteTaper high-performance Envoy external processor on Kubernetes. Built with Kubebuilder and controller-runtime, it introduces the declarative `ByteTaperGateway` Custom Resource.

## Quick Start (KinD)

```bash
# 1. Install CRDs into the cluster
kubectl apply -f config/crd/bases

# 2. Deploy the Operator manager into bytetaper-operator-system namespace
kubectl apply -k config/default

# 3. Create a policy ConfigMap
kubectl create namespace bytetaper
kubectl create configmap bytetaper-policy -n bytetaper \
  --from-file=policy.yaml=../../examples/policy/bytetaper-policy.yaml

# 4. Apply a ByteTaperGateway Custom Resource
kubectl apply -f config/samples/bytetaper_v1alpha1_bytetapergateway_local.yaml

# 5. Verify reconciliation
kubectl wait --for=condition=Ready bytetapergateway/bytetapergateway-local -n bytetaper --timeout=120s
```

## Helm vs Operator Comparison

| Feature | Helm Chart (`charts/bytetaper`) | Kubernetes Operator (`operator`) |
|---|---|---|
| **Primary Role** | Canonical deployment package | Advanced lifecycle automation |
| **Reconciliation** | Client-side, run once on upgrade | Server-side, continuous self-healing |
| **Drift Correction** | Manual (`helm upgrade`) | Automatic (periodic resync) |
| **Status Feedback** | None | Real-time conditions (`status.conditions`) |

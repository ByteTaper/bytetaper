# Azure Kubernetes Service (AKS) Production Example

This example demonstrates deploying the ByteTaper gateway into an Azure Kubernetes Service (AKS) cluster using the official Terraform module.

## Features
* **StorageClass `managed-premium`**: Azure Premium SSD persistent storage for optimal L2 caching performance.
* **Immutable Pinning**: Pinned via container SHA256 digest.

## Runbook

### 1. Authenticate with AKS Cluster
```bash
az aks get-credentials --resource-group my-resource-group --name my-production-cluster
```

### 2. Pre-create Policy ConfigMap
```bash
kubectl create namespace bytetaper-prod
kubectl create configmap bytetaper-policy --from-file=policy.yaml=./sample-policy.yaml -n bytetaper-prod
```

### 3. Deploy via Terraform
```bash
terraform init
terraform apply -var="image_digest=sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
```

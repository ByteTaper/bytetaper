# AWS EKS Production Deployment Example

This example demonstrates deploying the ByteTaper gateway into an AWS EKS production cluster using the official Terraform module.

## Features
* **StorageClass `gp3`**: Optimized EBS storage for high-throughput L2 caching.
* **Immutable Pinning**: Enforces deploying via SHA256 image digest.
* **ConfigMap Policy**: Decouples policy definition into an existing Kubernetes ConfigMap.

## Runbook

### 1. Authenticate with EKS Cluster
```bash
aws eks update-kubeconfig --region us-east-1 --name my-production-cluster
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

# Google Kubernetes Engine (GKE) Production Example

This example demonstrates deploying the ByteTaper gateway into a Google Kubernetes Engine (GKE) cluster using the official Terraform module.

## Features
* **StorageClass `standard-rwo`**: Balanced persistent disk backed by SSD/HDD on Google Cloud.
* **Immutable Pinning**: Pinned via container SHA256 digest.

## Runbook

### 1. Authenticate with GKE Cluster
```bash
gcloud container clusters get-credentials my-production-cluster --region us-central1
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

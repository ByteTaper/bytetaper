# Local Kind Deployment Example

This example demonstrates deploying the ByteTaper gateway to a local KinD (Kubernetes in Docker) cluster using the official Terraform module.

## Prerequisites
* Docker
* KinD (`kind`)
* Terraform (`>= 1.6.0`)
* `kubectl`

## Step-by-Step Runbook

### 1. Create KinD Cluster
```bash
kind create cluster --name bytetaper-dist003
```

### 2. Build and Load Image Locally
```bash
docker build -f ../../../docker/production.Dockerfile -t bytetaper-runtime:local ../../../
kind load docker-image bytetaper-runtime:local --name bytetaper-dist003
```

### 3. Deploy via Terraform
```bash
terraform init
terraform apply -auto-approve
```

### 4. Verify Rollout and Endpoints
```bash
kubectl rollout status deployment/bytetaper -n bytetaper
kubectl port-forward svc/bytetaper -n bytetaper 18081:18081 &
curl -fsS http://localhost:18081/healthz
curl -fsS http://localhost:18081/readyz
curl -fsS http://localhost:18081/metrics
```

### 5. Teardown
```bash
terraform destroy -auto-approve
kind delete cluster --name bytetaper-dist003
```

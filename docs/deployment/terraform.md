# Terraform Deployment Runbook

This runbook guides infrastructure operators through deploying and managing the ByteTaper API Performance Gateway using the official Terraform module.

## 1. Architectural Overview

The `bytetaper-helm` Terraform module provides a declarative HCL wrapper over the canonical Helm chart. It enforces hardened defaults (UID 1001, immutable digest pinning) while exposing clean abstractions for persistent cache storage and policy topology.

```hcl
module "bytetaper" {
  source = "github.com/ByteTaper/bytetaper//terraform/modules/bytetaper-helm?ref=v0.1.0"
  # ... configuration attributes
}
```

## 2. Policy Topologies

Operators can configure the data plane routing policy across three supported operational modes:

### Mode A: Inline YAML (Development/Small Scale)
```hcl
policy_mode = "inline"
policy_yaml = <<-EOT
  routes:
    - match: { prefix: "/" }
      upstream: "backend-svc:8080"
EOT
```

### Mode B: Existing ConfigMap (Recommended Production Pattern)
Decouples policy lifecycle from infrastructure deployment.
```hcl
policy_mode                    = "existingConfigMap"
existing_policy_configmap_name = "bytetaper-production-policy"
policy_key                     = "policy.yaml"
```

### Mode C: Existing Secret (Sensitive Routing/Credentials)
Mounts routing configuration from encrypted Kubernetes Secrets.
```hcl
policy_mode                 = "existingSecret"
existing_policy_secret_name = "bytetaper-secure-policy"
policy_key                  = "policy.yaml"
```

## 3. Persistent Storage Allocation

High-performance L2 caching requires dedicating persistent storage volumes matching cloud provider capabilities:

| Provider | Recommended StorageClass | Volume Type |
|---|---|---|
| AWS EKS | `gp3` | Optimized EBS SSD |
| Google GKE | `standard-rwo` / `pd-ssd` | Balanced or Premium Persistent Disk |
| Azure AKS | `managed-premium` | Premium Managed Disks |

```hcl
l2_cache_enabled            = true
l2_cache_size               = "50Gi"
l2_cache_storage_class_name = "gp3"
```

## 4. Operational Observability

### Metrics & Scraping
The module exposes Prometheus metrics on port 18081 by default. When deploying alongside the Prometheus Operator, enable ServiceMonitor integration:
```hcl
metrics_enabled         = true
service_monitor_enabled = true
```

### Admin Control Plane
For security compliance, the administrative control plane endpoint (port 18082) is completely disabled by default. If runtime introspection is required, explicitly enable it:
```hcl
admin_enabled         = true
admin_service_enabled = false # Keep internal to pod loopback
```

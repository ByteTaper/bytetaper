# ByteTaper Helm Terraform Module

This Terraform module provides an official, declarative wrapper around the ByteTaper API Performance Gateway Helm chart (`BT-DIST-003`).

## Features
* **Declarative Helm Values**: Fully typed HCL representation of image pinning, policy modes, persistent cache PVCs, compute resources, and observability endpoints.
* **Production Hardened Defaults**: Enforces UID 1001, disables the admin control plane by default, and supports immutable OCI image digest pinning.
* **Flexible Policy Topologies**: Supports inline YAML, existing Kubernetes ConfigMaps, or existing Secrets.

## Usage Example

```hcl
module "bytetaper" {
  source = "github.com/ByteTaper/bytetaper//terraform/modules/bytetaper-helm?ref=v0.1.0"

  release_name     = "bytetaper-prod"
  namespace        = "gateway-system"
  create_namespace = true

  chart_repository = "https://haluan.github.io/bytetaper-charts"
  chart_name       = "bytetaper"
  chart_version    = "0.1.0"

  image_repository = "ghcr.io/bytetaper/bytetaper-runtime"
  image_digest     = "sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"

  policy_mode = "inline"
  policy_yaml = <<-EOT
    routes:
      - match: { prefix: "/api" }
        upstream: "api-service:8080"
  EOT

  l2_cache_enabled            = true
  l2_cache_size               = "20Gi"
  l2_cache_storage_class_name = "gp3"

  resources = {
    requests = { cpu = "1000m", memory = "1Gi" }
    limits   = { cpu = "2000m", memory = "2Gi" }
  }

  metrics_enabled         = true
  service_monitor_enabled = true
}
```

## Inputs / Outputs
Refer to `variables.tf` and `outputs.tf` for complete attribute documentation.

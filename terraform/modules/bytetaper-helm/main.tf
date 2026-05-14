resource "kubernetes_namespace_v1" "this" {
  count = var.create_namespace ? 1 : 0

  metadata {
    name = var.namespace
  }
}

locals {
  base_values = {
    image = {
      repository = var.image_repository
      tag        = var.image_tag != "" ? var.image_tag : null
      digest     = var.image_digest != "" ? var.image_digest : null
    }

    policy = {
      mode   = var.policy_mode
      inline = var.policy_yaml != "" ? var.policy_yaml : null
      existingConfigMap = {
        name = var.existing_policy_configmap_name != "" ? var.existing_policy_configmap_name : null
        key  = var.policy_key
      }
      existingSecret = {
        name = var.existing_policy_secret_name != "" ? var.existing_policy_secret_name : null
        key  = var.policy_key
      }
    }

    l2Cache = {
      enabled = var.l2_cache_enabled
      persistence = {
        enabled          = var.l2_cache_enabled
        size             = var.l2_cache_size
        storageClassName = var.l2_cache_storage_class_name != "" ? var.l2_cache_storage_class_name : null
      }
    }

    resources = var.resources

    metrics = {
      enabled = var.metrics_enabled
      serviceMonitor = {
        enabled = var.service_monitor_enabled
      }
    }

    admin = {
      enabled = var.admin_enabled
      service = {
        enabled = var.admin_service_enabled
      }
    }
  }

  merged_values = merge(local.base_values, var.extra_values)
}

resource "helm_release" "bytetaper" {
  name       = var.release_name
  namespace  = var.create_namespace ? kubernetes_namespace_v1.this[0].metadata[0].name : var.namespace
  repository = var.chart_path == "" ? var.chart_repository : null
  chart      = var.chart_path != "" ? var.chart_path : var.chart_name
  version    = var.chart_path == "" ? var.chart_version : null

  values = [
    yamlencode(local.merged_values)
  ]
}

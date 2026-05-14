provider "helm" {
  kubernetes = {
    config_path = "~/.kube/config"
  }
}

provider "kubernetes" {
  config_path = "~/.kube/config"
}

module "bytetaper" {
  source = "../../modules/bytetaper-helm"

  release_name     = "bytetaper-eks"
  namespace        = var.namespace
  create_namespace = true

  chart_repository = var.chart_repository
  chart_name       = "bytetaper"
  chart_version    = var.chart_version

  image_repository = "ghcr.io/haluan/bytetaper-runtime"
  image_digest     = var.image_digest

  policy_mode                    = "existingConfigMap"
  existing_policy_configmap_name = var.existing_policy_configmap_name
  policy_key                     = "policy.yaml"

  l2_cache_enabled            = true
  l2_cache_size               = "20Gi"
  l2_cache_storage_class_name = "gp3"

  resources = {
    requests = { cpu = "1000m", memory = "1Gi" }
    limits   = { cpu = "2000m", memory = "2Gi" }
  }

  metrics_enabled         = true
  service_monitor_enabled = false
}

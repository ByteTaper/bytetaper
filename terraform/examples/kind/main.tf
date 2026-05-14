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

  release_name     = var.release_name
  namespace        = var.namespace
  create_namespace = true

  chart_path       = "../../../charts/bytetaper"
  image_repository = "bytetaper-runtime"
  image_tag        = var.image_tag

  policy_mode = "inline"
  policy_yaml = <<-EOT
    routes:
      - match: { prefix: "/" }
        upstream: "echo-server:8080"
  EOT

  l2_cache_enabled = false

  extra_values = yamldecode(file("${path.module}/values.yaml"))
}

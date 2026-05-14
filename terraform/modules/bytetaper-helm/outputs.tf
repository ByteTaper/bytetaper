output "namespace" {
  value       = helm_release.bytetaper.namespace
  description = "The Kubernetes namespace where ByteTaper was deployed."
}

output "release_name" {
  value       = helm_release.bytetaper.name
  description = "The Helm release name."
}

output "chart_version" {
  value       = helm_release.bytetaper.version
  description = "The deployed Helm chart version."
}

output "service_name" {
  value       = helm_release.bytetaper.name
  description = "The primary Kubernetes Service name for ByteTaper."
}

output "extproc_endpoint" {
  value       = "${helm_release.bytetaper.name}.${helm_release.bytetaper.namespace}.svc.cluster.local:18080"
  description = "The cluster-internal gRPC endpoint for Envoy ext_proc integration."
}

output "metrics_endpoint" {
  value       = "http://${helm_release.bytetaper.name}.${helm_release.bytetaper.namespace}.svc.cluster.local:18081/metrics"
  description = "The Prometheus scraping endpoint URL."
}

output "admin_enabled" {
  value       = var.admin_enabled
  description = "Whether the administrative control plane endpoint is enabled."
}

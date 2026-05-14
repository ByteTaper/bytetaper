output "namespace" {
  value       = module.bytetaper.namespace
  description = "The AKS namespace."
}

output "release_name" {
  value       = module.bytetaper.release_name
  description = "The Helm release name."
}

output "extproc_endpoint" {
  value       = module.bytetaper.extproc_endpoint
  description = "Cluster-internal gRPC endpoint on AKS."
}

output "metrics_endpoint" {
  value       = module.bytetaper.metrics_endpoint
  description = "Prometheus scraping endpoint on AKS."
}

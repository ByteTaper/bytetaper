variable "namespace" {
  type        = string
  default     = "bytetaper-prod"
  description = "Kubernetes namespace on GKE."
}

variable "chart_repository" {
  type        = string
  default     = "https://haluan.github.io/bytetaper-charts"
  description = "Helm chart repository URL."
}

variable "chart_version" {
  type        = string
  default     = "0.1.0"
  description = "Helm chart version."
}

variable "image_digest" {
  type        = string
  description = "Pinned SHA256 image digest for immutable production deployment."

  validation {
    condition     = startswith(var.image_digest, "sha256:")
    error_message = "The image_digest must start with 'sha256:'."
  }
}

variable "existing_policy_configmap_name" {
  type        = string
  default     = "bytetaper-policy"
  description = "Name of existing ConfigMap containing policy.yaml on GKE."
}

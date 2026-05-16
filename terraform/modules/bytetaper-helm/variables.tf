variable "namespace" {
  type        = string
  default     = "bytetaper"
  description = "Kubernetes namespace to deploy the ByteTaper Helm chart into."
}

variable "create_namespace" {
  type        = bool
  default     = true
  description = "Whether to create the Kubernetes namespace before deploying the Helm chart."
}

variable "release_name" {
  type        = string
  default     = "bytetaper"
  description = "Name of the Helm release."
}

variable "chart_repository" {
  type        = string
  default     = ""
  description = "Helm chart repository URL. Leave empty when using chart_path for local development."
}

variable "chart_name" {
  type        = string
  default     = "bytetaper"
  description = "Name of the Helm chart."
}

variable "chart_version" {
  type        = string
  default     = ""
  description = "Version of the Helm chart to deploy."
}

variable "chart_path" {
  type        = string
  default     = ""
  description = "Local filesystem path to the ByteTaper Helm chart. Overrides chart_repository if specified."
}

variable "image_repository" {
  type        = string
  default     = "ghcr.io/bytetaper/bytetaper-runtime"
  description = "Container image repository for the ByteTaper runtime."
}

variable "image_tag" {
  type        = string
  default     = ""
  description = "Container image tag. Prefer using image_digest for immutable production deployments."
}

variable "image_digest" {
  type        = string
  default     = ""
  description = "Container image SHA256 digest for immutable pinning (e.g., sha256:abcdef...)."
}

variable "policy_mode" {
  type        = string
  default     = "inline"
  description = "Mode for providing the ByteTaper configuration policy. Valid values: inline, existingConfigMap, existingSecret."

  validation {
    condition     = contains(["inline", "existingConfigMap", "existingSecret"], var.policy_mode)
    error_message = "The policy_mode variable must be one of: inline, existingConfigMap, existingSecret."
  }
}

variable "policy_yaml" {
  type        = string
  default     = ""
  sensitive   = true
  description = "Raw YAML policy content when policy_mode is 'inline'."
}

variable "existing_policy_configmap_name" {
  type        = string
  default     = ""
  description = "Name of the existing ConfigMap containing the policy when policy_mode is 'existingConfigMap'."
}

variable "existing_policy_secret_name" {
  type        = string
  default     = ""
  description = "Name of the existing Secret containing the policy when policy_mode is 'existingSecret'."
}

variable "policy_key" {
  type        = string
  default     = "policy.yaml"
  description = "Key within the existing ConfigMap or Secret containing the policy content."
}

variable "l2_cache_enabled" {
  type        = bool
  default     = true
  description = "Whether to enable persistent volume storage for the L2 cache."
}

variable "l2_cache_size" {
  type        = string
  default     = "10Gi"
  description = "Storage capacity allocated for the persistent L2 cache volume."
}

variable "l2_cache_storage_class_name" {
  type        = string
  default     = ""
  description = "StorageClass name for the L2 cache persistent volume claim."
}

variable "resources" {
  type = object({
    requests = optional(map(string), {})
    limits   = optional(map(string), {})
  })
  default     = {}
  description = "Kubernetes compute resource requests and limits for the ByteTaper container."
}

variable "metrics_enabled" {
  type        = bool
  default     = true
  description = "Whether to enable the Prometheus metrics endpoint on port 18081."
}

variable "service_monitor_enabled" {
  type        = bool
  default     = false
  description = "Whether to create a Prometheus Operator ServiceMonitor for automated metric scraping."
}

variable "admin_enabled" {
  type        = bool
  default     = false
  description = "Whether to enable the administrative control plane endpoint (disabled by default for security)."
}

variable "admin_service_enabled" {
  type        = bool
  default     = false
  description = "Whether to expose the admin endpoint via the Kubernetes Service (requires admin_enabled)."
}

variable "extra_values" {
  type        = any
  default     = {}
  description = "Arbitrary map of additional Helm values to merge into the deployment configuration."
}

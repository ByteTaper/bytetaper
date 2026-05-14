variable "namespace" {
  type        = string
  default     = "bytetaper"
  description = "Kubernetes namespace to deploy the ByteTaper example into."
}

variable "release_name" {
  type        = string
  default     = "bytetaper"
  description = "Helm release name for the example."
}

variable "image_tag" {
  type        = string
  default     = "local"
  description = "Tag of the locally built and loaded ByteTaper image."
}

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

/*
Copyright 2026 ByteTaper Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed/License is distributed on an "AS Shared" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

package v1alpha1

import (
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

const (
	ConditionReady            = "Ready"
	ConditionPolicyReferenced = "PolicyReferenced"
	ConditionPolicyMounted    = "PolicyMounted"
	ConditionCachePersistent  = "CachePersistent"
	ConditionDeploymentReady  = "DeploymentReady"
	ConditionServiceReady     = "ServiceReady"
	ConditionAdminExposed     = "AdminExposed"
)

type ByteTaperImageSpec struct {
	// Repository is the container image repository.
	// +kubebuilder:validation:Required
	// +kubebuilder:validation:MinLength=1
	// +kubebuilder:default="ghcr.io/bytetaper/bytetaper-runtime"
	Repository string `json:"repository,omitempty"`

	// Tag is the container image tag. Prefer Digest for production.
	Tag string `json:"tag,omitempty"`

	// Digest is the container image SHA256 digest pinning. Overrides Tag.
	// +kubebuilder:validation:Optional
	// +kubebuilder:validation:Pattern="^sha256:[a-f0-9]{64}$"
	Digest string `json:"digest,omitempty"`

	// PullPolicy is the container image pull policy.
	// +kubebuilder:default="IfNotPresent"
	PullPolicy corev1.PullPolicy `json:"pullPolicy,omitempty"`

	// PullSecrets is a list of secret names for pulling private images.
	PullSecrets []corev1.LocalObjectReference `json:"pullSecrets,omitempty"`
}

// +kubebuilder:validation:XValidation:rule="(has(self.configMapRef) && !has(self.secretRef)) || (!has(self.configMapRef) && has(self.secretRef))",message="exactly one of configMapRef or secretRef must be set"
type ByteTaperPolicySpec struct {
	// ConfigMapRef references a ConfigMap containing policy.yaml. Exactly one of ConfigMapRef or SecretRef must be set.
	ConfigMapRef *corev1.LocalObjectReference `json:"configMapRef,omitempty"`

	// SecretRef references a Secret containing policy.yaml.
	SecretRef *corev1.LocalObjectReference `json:"secretRef,omitempty"`

	// Key within the referenced ConfigMap or Secret containing the YAML policy.
	// +kubebuilder:validation:Required
	// +kubebuilder:validation:MinLength=1
	// +kubebuilder:default="policy.yaml"
	Key string `json:"key,omitempty"`
}

type ByteTaperPersistenceSpec struct {
	// Enabled indicates whether persistent volume claims should be provisioned.
	// +kubebuilder:default=true
	Enabled bool `json:"enabled,omitempty"`

	// StorageClassName to request for the persistent volume claim.
	StorageClassName string `json:"storageClassName,omitempty"`

	// Size of the persistent volume claim.
	// +kubebuilder:default="10Gi"
	Size string `json:"size,omitempty"`

	// AccessModes requested for the claim.
	// +kubebuilder:default={"ReadWriteOnce"}
	AccessModes []corev1.PersistentVolumeAccessMode `json:"accessModes,omitempty"`

	// ExistingClaim references an existing PVC to mount, skipping automated provisioning.
	ExistingClaim string `json:"existingClaim,omitempty"`
}

type ByteTaperL2CacheSpec struct {
	// Enabled indicates whether L2 caching is active.
	// +kubebuilder:default=true
	Enabled bool `json:"enabled,omitempty"`

	// Persistence configures persistent volume allocation for the L2 cache.
	Persistence ByteTaperPersistenceSpec `json:"persistence,omitempty"`
}

type ByteTaperRuntimeSpec struct {
	// ListenAddress for incoming Envoy ext_proc gRPC streams.
	// +kubebuilder:default="0.0.0.0:18080"
	ListenAddress string `json:"listenAddress,omitempty"`

	// ExtProcPort is the port number for the ext_proc gRPC service.
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=65535
	// +kubebuilder:default=18080
	ExtProcPort int32 `json:"extProcPort,omitempty"`

	// MetricsAddress for Prometheus metrics scraping.
	// +kubebuilder:default="0.0.0.0"
	MetricsAddress string `json:"metricsAddress,omitempty"`

	// MetricsPort for Prometheus metrics scraping.
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=65535
	// +kubebuilder:default=18081
	MetricsPort int32 `json:"metricsPort,omitempty"`
}

type ByteTaperMetricsSpec struct {
	// Enabled indicates whether the Prometheus metrics scraping endpoint is enabled.
	// +kubebuilder:default=true
	Enabled bool `json:"enabled,omitempty"`
}

// +kubebuilder:validation:XValidation:rule="!self.serviceEnabled || self.enabled",message="admin.serviceEnabled requires admin.enabled to be true"
type ByteTaperAdminSpec struct {
	// Enabled indicates whether the administrative control plane endpoint is active.
	// +kubebuilder:default=false
	Enabled bool `json:"enabled,omitempty"`

	// Address for the administrative endpoint.
	// +kubebuilder:default="127.0.0.1"
	Address string `json:"address,omitempty"`

	// Port for the administrative endpoint.
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=65535
	// +kubebuilder:default=18082
	Port int32 `json:"port,omitempty"`

	// ServiceEnabled indicates whether to create a Kubernetes ClusterIP Service for the admin endpoint.
	// +kubebuilder:default=false
	ServiceEnabled bool `json:"serviceEnabled,omitempty"`
}

type ByteTaperGatewaySpec struct {
	// Replicas is the desired number of gateway pods.
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:default=1
	Replicas *int32 `json:"replicas,omitempty"`

	// Image configuration.
	Image ByteTaperImageSpec `json:"image,omitempty"`

	// Policy configuration.
	Policy ByteTaperPolicySpec `json:"policy,omitempty"`

	// L2Cache configuration.
	L2Cache ByteTaperL2CacheSpec `json:"l2Cache,omitempty"`

	// Runtime contract configuration.
	Runtime ByteTaperRuntimeSpec `json:"runtime,omitempty"`

	// Metrics configuration.
	Metrics ByteTaperMetricsSpec `json:"metrics,omitempty"`

	// Admin control plane configuration.
	Admin ByteTaperAdminSpec `json:"admin,omitempty"`

	// Resources requests and limits.
	Resources corev1.ResourceRequirements `json:"resources,omitempty"`

	// ServiceAccountName for the gateway pods.
	ServiceAccountName string `json:"serviceAccountName,omitempty"`

	// NodeSelector for pod scheduling.
	NodeSelector map[string]string `json:"nodeSelector,omitempty"`

	// Tolerations for pod scheduling.
	Tolerations []corev1.Toleration `json:"tolerations,omitempty"`

	// Affinity for pod scheduling.
	Affinity *corev1.Affinity `json:"affinity,omitempty"`
}

type ByteTaperGatewayStatus struct {
	// ObservedGeneration is the most recent generation observed by the controller.
	ObservedGeneration int64 `json:"observedGeneration,omitempty"`

	// Phase represents the overall operational state.
	Phase string `json:"phase,omitempty"`

	// EffectiveImage reflects the actual container image reference running in the deployment.
	EffectiveImage string `json:"effectiveImage,omitempty"`

	// DeploymentName is the name of the managed Deployment.
	DeploymentName string `json:"deploymentName,omitempty"`

	// ServiceName is the name of the managed primary ClusterIP Service.
	ServiceName string `json:"serviceName,omitempty"`

	// ReadyReplicas is the number of ready gateway pods.
	ReadyReplicas int32 `json:"readyReplicas,omitempty"`

	// Conditions list current operational state conditions.
	Conditions []metav1.Condition `json:"conditions,omitempty"`
}

// +kubebuilder:object:root=true
// +kubebuilder:subresource:status
// +kubebuilder:printcolumn:name="Phase",type="string",JSONPath=".status.phase",description="Operational phase"
// +kubebuilder:printcolumn:name="Replicas",type="integer",JSONPath=".status.readyReplicas",description="Ready replicas"
// +kubebuilder:printcolumn:name="Image",type="string",JSONPath=".status.effectiveImage",description="Effective container image"

// ByteTaperGateway is the Schema for the bytetapergateways API
type ByteTaperGateway struct {
	metav1.TypeMeta   `json:",inline"`
	metav1.ObjectMeta `json:"metadata,omitempty"`

	Spec   ByteTaperGatewaySpec   `json:"spec,omitempty"`
	Status ByteTaperGatewayStatus `json:"status,omitempty"`
}

// +kubebuilder:object:root=true

// ByteTaperGatewayList contains a list of ByteTaperGateway
type ByteTaperGatewayList struct {
	metav1.TypeMeta `json:",inline"`
	metav1.ListMeta `json:"metadata,omitempty"`
	Items           []ByteTaperGateway `json:"items"`
}

func init() {
	SchemeBuilder.Register(&ByteTaperGateway{}, &ByteTaperGatewayList{})
}

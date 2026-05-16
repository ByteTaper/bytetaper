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

package controller

import (
	"context"
	"crypto/sha256"
	"fmt"
	"reflect"
	"time"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/api/meta"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/apimachinery/pkg/util/intstr"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"
	"sigs.k8s.io/controller-runtime/pkg/log"

	bytetaperv1alpha1 "github.com/ByteTaper/bytetaper/operator/api/v1alpha1"
)

const (
	gatewayFinalizer = "bytetaper.io/finalizer"
)

// ByteTaperGatewayReconciler reconciles a ByteTaperGateway object
type ByteTaperGatewayReconciler struct {
	client.Client
	Scheme *runtime.Scheme
}

// +kubebuilder:rbac:groups=bytetaper.io,resources=bytetapergateways,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=bytetaper.io,resources=bytetapergateways/status,verbs=get;update;patch
// +kubebuilder:rbac:groups=bytetaper.io,resources=bytetapergateways/finalizers,verbs=update
// +kubebuilder:rbac:groups=apps,resources=deployments,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=core,resources=services,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=core,resources=persistentvolumeclaims,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=core,resources=configmaps,verbs=get;list;watch
// +kubebuilder:rbac:groups=core,resources=secrets,verbs=get;list;watch
// +kubebuilder:rbac:groups=core,resources=events,verbs=create;patch

func (r *ByteTaperGatewayReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	logger := log.FromContext(ctx)

	var gw bytetaperv1alpha1.ByteTaperGateway
	if err := r.Get(ctx, req.NamespacedName, &gw); err != nil {
		if apierrors.IsNotFound(err) {
			return ctrl.Result{}, nil
		}
		return ctrl.Result{}, err
	}

	if gw.DeletionTimestamp != nil {
		return ctrl.Result{}, nil
	}

	// 1. Reconcile PVC
	if err := r.reconcilePVC(ctx, &gw); err != nil {
		logger.Error(err, "Failed to reconcile PVC")
		return r.updateStatusWithError(ctx, &gw, "PVCReconciliationFailed", err)
	}

	// 2. Reconcile Deployment
	if err := r.reconcileDeployment(ctx, &gw); err != nil {
		logger.Error(err, "Failed to reconcile Deployment")
		return r.updateStatusWithError(ctx, &gw, "DeploymentReconciliationFailed", err)
	}

	// 3. Reconcile Primary Service
	if err := r.reconcileService(ctx, &gw); err != nil {
		logger.Error(err, "Failed to reconcile Primary Service")
		return r.updateStatusWithError(ctx, &gw, "ServiceReconciliationFailed", err)
	}

	// 4. Reconcile Admin Service
	if err := r.reconcileAdminService(ctx, &gw); err != nil {
		logger.Error(err, "Failed to reconcile Admin Service")
		return r.updateStatusWithError(ctx, &gw, "AdminServiceReconciliationFailed", err)
	}

	// 5. Update Status
	return r.updateStatus(ctx, &gw)
}

func (r *ByteTaperGatewayReconciler) reconcilePVC(ctx context.Context, gw *bytetaperv1alpha1.ByteTaperGateway) error {
	if !gw.Spec.L2Cache.Enabled || !gw.Spec.L2Cache.Persistence.Enabled || gw.Spec.L2Cache.Persistence.ExistingClaim != "" {
		return nil
	}

	pvcName := fmt.Sprintf("%s-cache", gw.Name)
	pvc := &corev1.PersistentVolumeClaim{
		ObjectMeta: metav1.ObjectMeta{
			Name:      pvcName,
			Namespace: gw.Namespace,
		},
	}

	_, err := controllerutil.CreateOrUpdate(ctx, r.Client, pvc, func() error {
		if pvc.ObjectMeta.CreationTimestamp.IsZero() {
			if err := controllerutil.SetControllerReference(gw, pvc, r.Scheme); err != nil {
				return err
			}
			pvc.Labels = commonLabels(gw)

			accessModes := gw.Spec.L2Cache.Persistence.AccessModes
			if len(accessModes) == 0 {
				accessModes = []corev1.PersistentVolumeAccessMode{corev1.ReadWriteOnce}
			}

			sizeStr := gw.Spec.L2Cache.Persistence.Size
			if sizeStr == "" {
				sizeStr = "10Gi"
			}

			resourceList := corev1.ResourceList{
				corev1.ResourceStorage: resource.MustParse(sizeStr),
			}

			var storageClass *string
			if gw.Spec.L2Cache.Persistence.StorageClassName != "" {
				sc := gw.Spec.L2Cache.Persistence.StorageClassName
				storageClass = &sc
			}

			pvc.Spec = corev1.PersistentVolumeClaimSpec{
				AccessModes:      accessModes,
				Resources:        corev1.VolumeResourceRequirements{Requests: resourceList},
				StorageClassName: storageClass,
			}
		}
		return nil
	})

	return err
}

func (r *ByteTaperGatewayReconciler) reconcileDeployment(ctx context.Context, gw *bytetaperv1alpha1.ByteTaperGateway) error {
	deploy := &appsv1.Deployment{
		ObjectMeta: metav1.ObjectMeta{
			Name:      gw.Name,
			Namespace: gw.Namespace,
		},
	}

	_, err := controllerutil.CreateOrUpdate(ctx, r.Client, deploy, func() error {
		if err := controllerutil.SetControllerReference(gw, deploy, r.Scheme); err != nil {
			return err
		}

		labels := commonLabels(gw)
		deploy.Labels = labels

		replicas := int32(1)
		if gw.Spec.Replicas != nil {
			replicas = *gw.Spec.Replicas
		}

		// Runtime Args
		listenAddr := gw.Spec.Runtime.ListenAddress
		if listenAddr == "" {
			listenAddr = "0.0.0.0:18080"
		}
		metricsAddr := gw.Spec.Runtime.MetricsAddress
		if metricsAddr == "" {
			metricsAddr = "0.0.0.0"
		}
		metricsPort := gw.Spec.Runtime.MetricsPort
		if metricsPort == 0 {
			metricsPort = 18081
		}

		args := []string{
			"--listen-address", listenAddr,
			"--policy-file", "/etc/bytetaper/policy.yaml",
			"--l2-cache-path", "/var/lib/bytetaper/l2-cache",
			"--metrics-address", metricsAddr,
			"--metrics-port", fmt.Sprintf("%d", metricsPort),
		}

		if gw.Spec.Admin.Enabled {
			adminAddr := gw.Spec.Admin.Address
			if adminAddr == "" {
				adminAddr = "127.0.0.1"
			}
			adminPort := gw.Spec.Admin.Port
			if adminPort == 0 {
				adminPort = 18082
			}
			args = append(args, "--admin-enable-taperquery", "--admin-address", adminAddr, "--admin-port", fmt.Sprintf("%d", adminPort))
		}

		// Security Context
		runAsUser := int64(1001)
		runAsNonRoot := true
		readOnlyRoot := true
		allowPrivilegeEscalation := false

		podSecContext := &corev1.PodSecurityContext{
			RunAsUser:    &runAsUser,
			RunAsGroup:   &runAsUser,
			FSGroup:      &runAsUser,
			RunAsNonRoot: &runAsNonRoot,
		}

		containerSecContext := &corev1.SecurityContext{
			ReadOnlyRootFilesystem:   &readOnlyRoot,
			AllowPrivilegeEscalation: &allowPrivilegeEscalation,
			Capabilities: &corev1.Capabilities{
				Drop: []corev1.Capability{"ALL"},
			},
		}

		// Volumes & Mounts
		var volumes []corev1.Volume
		var mounts []corev1.VolumeMount

		// 1. Policy Volume
		policyKey := gw.Spec.Policy.Key
		if policyKey == "" {
			policyKey = "policy.yaml"
		}
		policyVol := corev1.Volume{Name: "policy"}
		if gw.Spec.Policy.ConfigMapRef != nil {
			policyVol.VolumeSource = corev1.VolumeSource{
				ConfigMap: &corev1.ConfigMapVolumeSource{
					LocalObjectReference: *gw.Spec.Policy.ConfigMapRef,
					Items: []corev1.KeyToPath{
						{Key: policyKey, Path: "policy.yaml"},
					},
				},
			}
		} else if gw.Spec.Policy.SecretRef != nil {
			policyVol.VolumeSource = corev1.VolumeSource{
				Secret: &corev1.SecretVolumeSource{
					SecretName: gw.Spec.Policy.SecretRef.Name,
					Items: []corev1.KeyToPath{
						{Key: policyKey, Path: "policy.yaml"},
					},
				},
			}
		} else {
			return fmt.Errorf("either configMapRef or secretRef must be specified in policy spec")
		}
		volumes = append(volumes, policyVol)
		mounts = append(mounts, corev1.VolumeMount{
			Name:      "policy",
			MountPath: "/etc/bytetaper/policy.yaml",
			SubPath:   "policy.yaml",
			ReadOnly:  true,
		})

		// 2. L2 Cache Volume
		cacheVol := corev1.Volume{Name: "cache"}
		if gw.Spec.L2Cache.Enabled {
			if gw.Spec.L2Cache.Persistence.ExistingClaim != "" {
				cacheVol.VolumeSource = corev1.VolumeSource{
					PersistentVolumeClaim: &corev1.PersistentVolumeClaimVolumeSource{
						ClaimName: gw.Spec.L2Cache.Persistence.ExistingClaim,
					},
				}
			} else if gw.Spec.L2Cache.Persistence.Enabled {
				cacheVol.VolumeSource = corev1.VolumeSource{
					PersistentVolumeClaim: &corev1.PersistentVolumeClaimVolumeSource{
						ClaimName: fmt.Sprintf("%s-cache", gw.Name),
					},
				}
			} else {
				cacheVol.VolumeSource = corev1.VolumeSource{EmptyDir: &corev1.EmptyDirVolumeSource{}}
			}
		} else {
			cacheVol.VolumeSource = corev1.VolumeSource{EmptyDir: &corev1.EmptyDirVolumeSource{}}
		}
		volumes = append(volumes, cacheVol)
		mounts = append(mounts, corev1.VolumeMount{
			Name:      "cache",
			MountPath: "/var/lib/bytetaper/l2-cache",
		})

		// 3. Runtime Run Volume
		runVol := corev1.Volume{
			Name: "run",
			VolumeSource: corev1.VolumeSource{
				EmptyDir: &corev1.EmptyDirVolumeSource{},
			},
		}
		volumes = append(volumes, runVol)
		mounts = append(mounts, corev1.VolumeMount{
			Name:      "run",
			MountPath: "/var/run/bytetaper",
		})

		podAnnotations := map[string]string{
			"bytetaper.io/policy-ref-hash": policyRefHash(gw.Spec.Policy),
		}

		extProcPort := gw.Spec.Runtime.ExtProcPort
		if extProcPort == 0 {
			extProcPort = 18080
		}

		containerPorts := []corev1.ContainerPort{
			{Name: "extproc", ContainerPort: extProcPort},
			{Name: "metrics", ContainerPort: metricsPort},
		}

		if gw.Spec.Admin.Enabled {
			adminPort := gw.Spec.Admin.Port
			if adminPort == 0 {
				adminPort = 18082
			}
			containerPorts = append(containerPorts, corev1.ContainerPort{Name: "admin", ContainerPort: adminPort})
		}

		deploy.Spec = appsv1.DeploymentSpec{
			Replicas: &replicas,
			Selector: &metav1.LabelSelector{MatchLabels: labels},
			Template: corev1.PodTemplateSpec{
				ObjectMeta: metav1.ObjectMeta{
					Labels:      labels,
					Annotations: podAnnotations,
				},
				Spec: corev1.PodSpec{
					ServiceAccountName: gw.Spec.ServiceAccountName,
					SecurityContext:    podSecContext,
					NodeSelector:       gw.Spec.NodeSelector,
					Tolerations:        gw.Spec.Tolerations,
					Affinity:           gw.Spec.Affinity,
					ImagePullSecrets:   gw.Spec.Image.PullSecrets,
					Containers: []corev1.Container{
						{
							Name:            "bytetaper",
							Image:           imageReference(gw.Spec.Image),
							ImagePullPolicy: gw.Spec.Image.PullPolicy,
							Args:            args,
							Ports:           containerPorts,
							SecurityContext: containerSecContext,
							Resources:       gw.Spec.Resources,
							VolumeMounts:    mounts,
							LivenessProbe: &corev1.Probe{
								ProbeHandler: corev1.ProbeHandler{
									HTTPGet: &corev1.HTTPGetAction{Path: "/healthz", Port: intstr.FromInt32(metricsPort)},
								},
								InitialDelaySeconds: 10,
								PeriodSeconds:       10,
								TimeoutSeconds:      2,
								FailureThreshold:    6,
							},
							ReadinessProbe: &corev1.Probe{
								ProbeHandler: corev1.ProbeHandler{
									HTTPGet: &corev1.HTTPGetAction{Path: "/readyz", Port: intstr.FromInt32(metricsPort)},
								},
								InitialDelaySeconds: 5,
								PeriodSeconds:       5,
								TimeoutSeconds:      2,
								FailureThreshold:    6,
							},
						},
					},
					Volumes: volumes,
				},
			},
		}

		return nil
	})

	return err
}

func (r *ByteTaperGatewayReconciler) reconcileService(ctx context.Context, gw *bytetaperv1alpha1.ByteTaperGateway) error {
	svc := &corev1.Service{
		ObjectMeta: metav1.ObjectMeta{
			Name:      gw.Name,
			Namespace: gw.Namespace,
		},
	}

	_, err := controllerutil.CreateOrUpdate(ctx, r.Client, svc, func() error {
		if err := controllerutil.SetControllerReference(gw, svc, r.Scheme); err != nil {
			return err
		}

		labels := commonLabels(gw)
		svc.Labels = labels

		extProcPort := gw.Spec.Runtime.ExtProcPort
		if extProcPort == 0 {
			extProcPort = 18080
		}

		metricsPort := gw.Spec.Runtime.MetricsPort
		if metricsPort == 0 {
			metricsPort = 18081
		}

		// Retain ClusterIP if already set
		clusterIP := svc.Spec.ClusterIP

		svc.Spec = corev1.ServiceSpec{
			Selector: labels,
			Type:     corev1.ServiceTypeClusterIP,
			Ports: []corev1.ServicePort{
				{Name: "extproc", Port: extProcPort, TargetPort: intstr.FromInt32(extProcPort)},
				{Name: "metrics", Port: metricsPort, TargetPort: intstr.FromInt32(metricsPort)},
			},
		}

		if clusterIP != "" {
			svc.Spec.ClusterIP = clusterIP
		}

		return nil
	})

	return err
}

func (r *ByteTaperGatewayReconciler) reconcileAdminService(ctx context.Context, gw *bytetaperv1alpha1.ByteTaperGateway) error {
	adminSvcName := fmt.Sprintf("%s-admin", gw.Name)
	svc := &corev1.Service{
		ObjectMeta: metav1.ObjectMeta{
			Name:      adminSvcName,
			Namespace: gw.Namespace,
		},
	}

	if !gw.Spec.Admin.Enabled || !gw.Spec.Admin.ServiceEnabled {
		err := r.Get(ctx, types.NamespacedName{Name: adminSvcName, Namespace: gw.Namespace}, svc)
		if err == nil {
			return r.Delete(ctx, svc)
		}
		if !apierrors.IsNotFound(err) {
			return err
		}
		return nil
	}

	_, err := controllerutil.CreateOrUpdate(ctx, r.Client, svc, func() error {
		if err := controllerutil.SetControllerReference(gw, svc, r.Scheme); err != nil {
			return err
		}

		labels := commonLabels(gw)
		svc.Labels = labels

		adminPort := gw.Spec.Admin.Port
		if adminPort == 0 {
			adminPort = 18082
		}

		clusterIP := svc.Spec.ClusterIP

		svc.Spec = corev1.ServiceSpec{
			Selector: labels,
			Type:     corev1.ServiceTypeClusterIP,
			Ports: []corev1.ServicePort{
				{Name: "admin", Port: adminPort, TargetPort: intstr.FromInt32(adminPort)},
			},
		}

		if clusterIP != "" {
			svc.Spec.ClusterIP = clusterIP
		}

		return nil
	})

	return err
}

func (r *ByteTaperGatewayReconciler) updateStatus(ctx context.Context, gw *bytetaperv1alpha1.ByteTaperGateway) (ctrl.Result, error) {
	status := gw.Status.DeepCopy()
	status.ObservedGeneration = gw.Generation
	status.EffectiveImage = imageReference(gw.Spec.Image)
	status.DeploymentName = gw.Name
	status.ServiceName = gw.Name

	// Check Policy Existence
	policyExists := false
	if gw.Spec.Policy.ConfigMapRef != nil {
		var cm corev1.ConfigMap
		err := r.Get(ctx, types.NamespacedName{Name: gw.Spec.Policy.ConfigMapRef.Name, Namespace: gw.Namespace}, &cm)
		if err == nil {
			policyExists = true
		}
	} else if gw.Spec.Policy.SecretRef != nil {
		var sec corev1.Secret
		err := r.Get(ctx, types.NamespacedName{Name: gw.Spec.Policy.SecretRef.Name, Namespace: gw.Namespace}, &sec)
		if err == nil {
			policyExists = true
		}
	}

	if policyExists {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionPolicyReferenced,
			Status:  metav1.ConditionTrue,
			Reason:  "PolicyFound",
			Message: "Referenced ConfigMap or Secret exists",
		})
	} else {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionPolicyReferenced,
			Status:  metav1.ConditionFalse,
			Reason:  "PolicyNotFound",
			Message: "Referenced ConfigMap or Secret not found",
		})
	}

	// Check Deployment Replicas & PolicyMounted
	var deploy appsv1.Deployment
	err := r.Get(ctx, types.NamespacedName{Name: gw.Name, Namespace: gw.Namespace}, &deploy)
	if err == nil {
		status.ReadyReplicas = deploy.Status.ReadyReplicas
		if deploy.Status.ReadyReplicas > 0 {
			meta.SetStatusCondition(&status.Conditions, metav1.Condition{
				Type:    bytetaperv1alpha1.ConditionDeploymentReady,
				Status:  metav1.ConditionTrue,
				Reason:  "ReplicasReady",
				Message: fmt.Sprintf("%d replicas ready", deploy.Status.ReadyReplicas),
			})
		} else {
			meta.SetStatusCondition(&status.Conditions, metav1.Condition{
				Type:    bytetaperv1alpha1.ConditionDeploymentReady,
				Status:  metav1.ConditionFalse,
				Reason:  "WaitingReplicas",
				Message: "Waiting for ready replicas",
			})
		}

		// Check PolicyMounted
		mounted := false
		if len(deploy.Spec.Template.Spec.Containers) > 0 {
			for _, m := range deploy.Spec.Template.Spec.Containers[0].VolumeMounts {
				if m.Name == "policy" && m.MountPath == "/etc/bytetaper/policy.yaml" {
					mounted = true
					break
				}
			}
		}
		if mounted {
			meta.SetStatusCondition(&status.Conditions, metav1.Condition{
				Type:    bytetaperv1alpha1.ConditionPolicyMounted,
				Status:  metav1.ConditionTrue,
				Reason:  "PolicyVolumeMounted",
				Message: "Policy volume is mounted read-only at expected path",
			})
		} else {
			meta.SetStatusCondition(&status.Conditions, metav1.Condition{
				Type:    bytetaperv1alpha1.ConditionPolicyMounted,
				Status:  metav1.ConditionFalse,
				Reason:  "PolicyVolumeNotMounted",
				Message: "Policy volume mount is missing",
			})
		}
	} else {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionDeploymentReady,
			Status:  metav1.ConditionFalse,
			Reason:  "DeploymentNotFound",
			Message: "Deployment not found",
		})
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionPolicyMounted,
			Status:  metav1.ConditionFalse,
			Reason:  "DeploymentNotFound",
			Message: "Deployment not found",
		})
	}

	// Check ServiceReady
	var svc corev1.Service
	err = r.Get(ctx, types.NamespacedName{Name: gw.Name, Namespace: gw.Namespace}, &svc)
	if err == nil && len(svc.Spec.Ports) >= 2 {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionServiceReady,
			Status:  metav1.ConditionTrue,
			Reason:  "ServicePortsReady",
			Message: "Primary service configured with extproc and metrics ports",
		})
	} else {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionServiceReady,
			Status:  metav1.ConditionFalse,
			Reason:  "ServiceNotReady",
			Message: "Primary service missing or incomplete",
		})
	}

	// Check PVC Status
	if gw.Spec.L2Cache.Enabled && gw.Spec.L2Cache.Persistence.Enabled && gw.Spec.L2Cache.Persistence.ExistingClaim == "" {
		pvcName := fmt.Sprintf("%s-cache", gw.Name)
		var pvc corev1.PersistentVolumeClaim
		err := r.Get(ctx, types.NamespacedName{Name: pvcName, Namespace: gw.Namespace}, &pvc)
		if err == nil && pvc.Status.Phase == corev1.ClaimBound {
			meta.SetStatusCondition(&status.Conditions, metav1.Condition{
				Type:    bytetaperv1alpha1.ConditionCachePersistent,
				Status:  metav1.ConditionTrue,
				Reason:  "ClaimBound",
				Message: "PVC is Bound",
			})
		} else {
			meta.SetStatusCondition(&status.Conditions, metav1.Condition{
				Type:    bytetaperv1alpha1.ConditionCachePersistent,
				Status:  metav1.ConditionFalse,
				Reason:  "ClaimNotBound",
				Message: "PVC is not Bound",
			})
		}
	} else {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionCachePersistent,
			Status:  metav1.ConditionTrue,
			Reason:  "PersistenceDisabledOrExisting",
			Message: "Persistence disabled or using existing claim",
		})
	}

	// Admin Exposed Status
	if gw.Spec.Admin.Enabled && gw.Spec.Admin.ServiceEnabled {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionAdminExposed,
			Status:  metav1.ConditionTrue,
			Reason:  "AdminServiceEnabled",
			Message: "Administrative service exposed",
		})
	} else {
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionAdminExposed,
			Status:  metav1.ConditionFalse,
			Reason:  "AdminServiceDisabled",
			Message: "Administrative service disabled",
		})
	}

	// Overall Ready Status
	readyCond := meta.FindStatusCondition(status.Conditions, bytetaperv1alpha1.ConditionDeploymentReady)
	policyCond := meta.FindStatusCondition(status.Conditions, bytetaperv1alpha1.ConditionPolicyReferenced)

	if readyCond != nil && readyCond.Status == metav1.ConditionTrue && policyCond != nil && policyCond.Status == metav1.ConditionTrue {
		status.Phase = "Ready"
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionReady,
			Status:  metav1.ConditionTrue,
			Reason:  "GatewayOperatingReady",
			Message: "Gateway is ready and serving traffic",
		})
	} else {
		status.Phase = "Progressing"
		meta.SetStatusCondition(&status.Conditions, metav1.Condition{
			Type:    bytetaperv1alpha1.ConditionReady,
			Status:  metav1.ConditionFalse,
			Reason:  "DependenciesNotReady",
			Message: "Gateway is awaiting dependencies or rollout",
		})
	}

	if !reflect.DeepEqual(gw.Status, *status) {
		gw.Status = *status
		if err := r.Status().Update(ctx, gw); err != nil {
			return ctrl.Result{}, err
		}
	}

	return ctrl.Result{RequeueAfter: 30 * time.Second}, nil
}

func (r *ByteTaperGatewayReconciler) updateStatusWithError(ctx context.Context, gw *bytetaperv1alpha1.ByteTaperGateway, reason string, err error) (ctrl.Result, error) {
	status := gw.Status.DeepCopy()
	status.Phase = "Failed"
	meta.SetStatusCondition(&status.Conditions, metav1.Condition{
		Type:    bytetaperv1alpha1.ConditionReady,
		Status:  metav1.ConditionFalse,
		Reason:  reason,
		Message: err.Error(),
	})

	if !reflect.DeepEqual(gw.Status, *status) {
		gw.Status = *status
		_ = r.Status().Update(ctx, gw)
	}

	return ctrl.Result{}, err
}

func commonLabels(gw *bytetaperv1alpha1.ByteTaperGateway) map[string]string {
	return map[string]string{
		"app.kubernetes.io/name":       "bytetaper",
		"app.kubernetes.io/instance":   gw.Name,
		"app.kubernetes.io/managed-by": "bytetaper-operator",
		"app.kubernetes.io/component":  "gateway",
	}
}

func imageReference(spec bytetaperv1alpha1.ByteTaperImageSpec) string {
	repo := spec.Repository
	if repo == "" {
		repo = "ghcr.io/bytetaper/bytetaper-runtime"
	}
	if spec.Digest != "" {
		return fmt.Sprintf("%s@%s", repo, spec.Digest)
	}
	tag := spec.Tag
	if tag == "" {
		tag = "latest"
	}
	return fmt.Sprintf("%s:%s", repo, tag)
}

func policyRefHash(policy bytetaperv1alpha1.ByteTaperPolicySpec) string {
	var refStr string
	if policy.ConfigMapRef != nil {
		refStr = fmt.Sprintf("cm/%s/%s", policy.ConfigMapRef.Name, policy.Key)
	} else if policy.SecretRef != nil {
		refStr = fmt.Sprintf("sec/%s/%s", policy.SecretRef.Name, policy.Key)
	}
	hash := sha256.Sum256([]byte(refStr))
	return fmt.Sprintf("%x", hash[:8])
}

func (r *ByteTaperGatewayReconciler) SetupWithManager(mgr ctrl.Manager) error {
	return ctrl.NewControllerManagedBy(mgr).
		For(&bytetaperv1alpha1.ByteTaperGateway{}).
		Owns(&appsv1.Deployment{}).
		Owns(&corev1.Service{}).
		Owns(&corev1.PersistentVolumeClaim{}).
		Complete(r)
}

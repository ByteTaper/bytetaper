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
	"strings"
	"testing"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/meta"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	utilruntime "k8s.io/apimachinery/pkg/util/runtime"
	clientgoscheme "k8s.io/client-go/kubernetes/scheme"
	"sigs.k8s.io/controller-runtime/pkg/client/fake"

	bytetaperv1alpha1 "github.com/ByteTaper/bytetaper/operator/api/v1alpha1"
)

func TestImageReference(t *testing.T) {
	tests := []struct {
		name     string
		spec     bytetaperv1alpha1.ByteTaperImageSpec
		expected string
	}{
		{
			name: "default values",
			spec: bytetaperv1alpha1.ByteTaperImageSpec{
				Repository: "",
				Tag:        "",
			},
			expected: "ghcr.io/ByteTaper/bytetaper-runtime:latest",
		},
		{
			name: "custom tag",
			spec: bytetaperv1alpha1.ByteTaperImageSpec{
				Repository: "custom-repo",
				Tag:        "v1.0.0",
			},
			expected: "custom-repo:v1.0.0",
		},
		{
			name: "digest override",
			spec: bytetaperv1alpha1.ByteTaperImageSpec{
				Repository: "custom-repo",
				Tag:        "v1.0.0",
				Digest:     "sha256:0123456789abcdef",
			},
			expected: "custom-repo@sha256:0123456789abcdef",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := imageReference(tt.spec)
			if got != tt.expected {
				t.Errorf("imageReference() = %v, want %v", got, tt.expected)
			}
		})
	}
}

func TestPolicyRefHash(t *testing.T) {
	spec1 := bytetaperv1alpha1.ByteTaperPolicySpec{
		ConfigMapRef: &corev1.LocalObjectReference{Name: "cm-policy"},
		Key:          "policy.yaml",
	}
	spec2 := bytetaperv1alpha1.ByteTaperPolicySpec{
		ConfigMapRef: &corev1.LocalObjectReference{Name: "cm-policy"},
		Key:          "policy.yaml",
	}
	spec3 := bytetaperv1alpha1.ByteTaperPolicySpec{
		SecretRef: &corev1.LocalObjectReference{Name: "sec-policy"},
		Key:       "rules.yaml",
	}

	hash1 := policyRefHash(spec1)
	hash2 := policyRefHash(spec2)
	hash3 := policyRefHash(spec3)

	if hash1 != hash2 {
		t.Errorf("policyRefHash() expected stable results: %v != %v", hash1, hash2)
	}
	if hash1 == hash3 {
		t.Errorf("policyRefHash() expected different results for different refs: %v == %v", hash1, hash3)
	}
}

func TestReconcilerLogic(t *testing.T) {
	scheme := runtime.NewScheme()
	utilruntime.Must(clientgoscheme.AddToScheme(scheme))
	utilruntime.Must(bytetaperv1alpha1.AddToScheme(scheme))

	gw := &bytetaperv1alpha1.ByteTaperGateway{
		ObjectMeta: metav1.ObjectMeta{
			Name:      "test-gw",
			Namespace: "default",
		},
		Spec: bytetaperv1alpha1.ByteTaperGatewaySpec{
			Policy: bytetaperv1alpha1.ByteTaperPolicySpec{
				ConfigMapRef: &corev1.LocalObjectReference{Name: "cm-policy"},
				Key:          "custom-policy.yaml",
			},
			L2Cache: bytetaperv1alpha1.ByteTaperL2CacheSpec{
				Enabled: true,
				Persistence: bytetaperv1alpha1.ByteTaperPersistenceSpec{
					Enabled: true,
					Size:    "20Gi",
				},
			},
			Admin: bytetaperv1alpha1.ByteTaperAdminSpec{
				Enabled:        true,
				ServiceEnabled: true,
				Port:           18082,
			},
		},
	}

	client := fake.NewClientBuilder().WithScheme(scheme).WithObjects(gw).WithStatusSubresource(gw).Build()
	reconciler := &ByteTaperGatewayReconciler{
		Client: client,
		Scheme: scheme,
	}

	ctx := context.TODO()

	// Reconcile Deployment
	err := reconciler.reconcileDeployment(ctx, gw)
	if err != nil {
		t.Fatalf("reconcileDeployment() failed: %v", err)
	}

	var deploy appsv1.Deployment
	if err := client.Get(ctx, types.NamespacedName{Name: "test-gw", Namespace: "default"}, &deploy); err != nil {
		t.Fatalf("failed to fetch created Deployment: %v", err)
	}

	container := deploy.Spec.Template.Spec.Containers[0]
	argsStr := strings.Join(container.Args, " ")
	if !strings.Contains(argsStr, "--admin-enable-taperquery") {
		t.Errorf("expected --admin-enable-taperquery in container args, got: %s", argsStr)
	}
	if !strings.Contains(argsStr, "--admin-port 18082") {
		t.Errorf("expected admin args in container args, got: %s", argsStr)
	}

	if container.SecurityContext == nil || !*container.SecurityContext.ReadOnlyRootFilesystem {
		t.Errorf("expected ReadOnlyRootFilesystem to be true")
	}

	// Verify volume projection mapping & mount
	foundVol := false
	for _, vol := range deploy.Spec.Template.Spec.Volumes {
		if vol.Name == "policy" && vol.ConfigMap != nil {
			foundVol = true
			if len(vol.ConfigMap.Items) == 0 || vol.ConfigMap.Items[0].Key != "custom-policy.yaml" || vol.ConfigMap.Items[0].Path != "policy.yaml" {
				t.Errorf("unexpected ConfigMap volume projection items: %v", vol.ConfigMap.Items)
			}
		}
	}
	if !foundVol {
		t.Errorf("expected policy ConfigMap volume projection not found")
	}

	foundMount := false
	for _, m := range container.VolumeMounts {
		if m.Name == "policy" && m.MountPath == "/etc/bytetaper/policy.yaml" && m.SubPath == "policy.yaml" {
			foundMount = true
		}
	}
	if !foundMount {
		t.Errorf("expected policy volume mount at /etc/bytetaper/policy.yaml with SubPath policy.yaml not found")
	}

	// Reconcile Primary Service
	err = reconciler.reconcileService(ctx, gw)
	if err != nil {
		t.Fatalf("reconcileService() failed: %v", err)
	}

	// Reconcile Admin Service
	err = reconciler.reconcileAdminService(ctx, gw)
	if err != nil {
		t.Fatalf("reconcileAdminService() failed: %v", err)
	}

	var adminSvc corev1.Service
	if err := client.Get(ctx, types.NamespacedName{Name: "test-gw-admin", Namespace: "default"}, &adminSvc); err != nil {
		t.Fatalf("failed to fetch created Admin Service: %v", err)
	}
	if adminSvc.Spec.Ports[0].Port != 18082 {
		t.Errorf("expected Admin service port 18082, got %d", adminSvc.Spec.Ports[0].Port)
	}

	// Reconcile Status & verify conditions
	_, err = reconciler.updateStatus(ctx, gw)
	if err != nil {
		t.Fatalf("updateStatus() failed: %v", err)
	}

	condMounted := meta.FindStatusCondition(gw.Status.Conditions, bytetaperv1alpha1.ConditionPolicyMounted)
	if condMounted == nil || condMounted.Status != metav1.ConditionTrue {
		t.Errorf("expected PolicyMounted condition True, got %v", condMounted)
	}

	condSvc := meta.FindStatusCondition(gw.Status.Conditions, bytetaperv1alpha1.ConditionServiceReady)
	if condSvc == nil || condSvc.Status != metav1.ConditionTrue {
		t.Errorf("expected ServiceReady condition True, got %v", condSvc)
	}

	// Switch to Secret Ref and verify
	gw.Spec.Policy = bytetaperv1alpha1.ByteTaperPolicySpec{
		SecretRef: &corev1.LocalObjectReference{Name: "sec-policy"},
		Key:       "secret-policy.yaml",
	}
	err = reconciler.reconcileDeployment(ctx, gw)
	if err != nil {
		t.Fatalf("reconcileDeployment() with SecretRef failed: %v", err)
	}

	if err := client.Get(ctx, types.NamespacedName{Name: "test-gw", Namespace: "default"}, &deploy); err != nil {
		t.Fatalf("failed to fetch updated Deployment: %v", err)
	}

	foundSecVol := false
	for _, vol := range deploy.Spec.Template.Spec.Volumes {
		if vol.Name == "policy" && vol.Secret != nil {
			foundSecVol = true
			if len(vol.Secret.Items) == 0 || vol.Secret.Items[0].Key != "secret-policy.yaml" || vol.Secret.Items[0].Path != "policy.yaml" {
				t.Errorf("unexpected Secret volume projection items: %v", vol.Secret.Items)
			}
		}
	}
	if !foundSecVol {
		t.Errorf("expected policy Secret volume projection not found")
	}

	// Disable Admin & Reconcile again -> should delete Admin Service
	gw.Spec.Admin.Enabled = false
	err = reconciler.reconcileAdminService(ctx, gw)
	if err != nil {
		t.Fatalf("reconcileAdminService() disable failed: %v", err)
	}

	err = client.Get(ctx, types.NamespacedName{Name: "test-gw-admin", Namespace: "default"}, &adminSvc)
	if err == nil {
		t.Errorf("expected Admin Service to be deleted")
	}
}

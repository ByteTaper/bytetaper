#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

echo "==> Packaging Helm chart..."

mkdir -p dist/release

echo "--> Updating chart dependencies..."
helm dependency update charts/bytetaper

echo "--> Linting chart..."
helm lint charts/bytetaper

echo "--> Validating template permutations and production examples..."
helm template bytetaper charts/bytetaper >/dev/null
helm template bytetaper charts/bytetaper --set policy.mode=existingConfigMap --set policy.existingConfigMap.name=test-policy --set policy.existingConfigMap.key=policy.yaml >/dev/null
helm template bytetaper charts/bytetaper --set admin.enabled=true --set admin.service.enabled=true >/dev/null

# Validate new production examples
helm template bytetaper charts/bytetaper --values charts/bytetaper/examples/single-replica-prod-values.yaml >/dev/null
helm template bytetaper charts/bytetaper --values charts/bytetaper/examples/multi-replica-ha-values.yaml >/dev/null
helm template bytetaper charts/bytetaper --values charts/bytetaper/examples/multi-replica-emptydir-values.yaml >/dev/null
helm template bytetaper charts/bytetaper --values charts/bytetaper/examples/values-production.yaml >/dev/null
helm template bytetaper charts/bytetaper --values charts/bytetaper/examples/network-policy-values.yaml >/dev/null

echo "--> Creating Helm package..."
PKG_PATH=$(helm package charts/bytetaper --destination dist/release | awk '{print $NF}')

echo "PASS: Helm chart packaged successfully: ${PKG_PATH}"

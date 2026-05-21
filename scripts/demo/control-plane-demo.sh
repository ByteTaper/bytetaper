#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/test/_control_plane_compose_lib.sh
source "${ROOT}/scripts/test/_control_plane_compose_lib.sh"

bytetaper_compose_init

export BYTETAPER_CP_COMPOSE_CLEAN="${BYTETAPER_CP_COMPOSE_CLEAN:-0}"
export BYTETAPER_CP_COMPOSE_REUSE_READY="${BYTETAPER_CP_COMPOSE_REUSE_READY:-1}"
export BYTETAPER_RUNTIME_METRICS_URL="${BYTETAPER_RUNTIME_METRICS_URL:-http://127.0.0.1:18083}"

CP_URL="$(cp_url)"
ENVOY_URL="$(envoy_url)"
APPLY_YAML="${ROOT}/examples/taperquery/apply-policy-source.yaml"

cp_start_profile

echo "==> Control Plane policy/current (bootstrap seeds generation 1 when store is empty)"
curl -fsS "${CP_URL}/admin/control-plane/policy/current" | jq .

echo "==> Applying policy through Control Plane (CAS from current committed pointer)"
cp_apply_policy_yaml "${APPLY_YAML}" "cp-demo-apply-1" "control-plane-demo" | jq .

echo "==> Waiting for runtime pull/convergence"
poll_fleet_converged 90

echo "==> Fleet status"
curl -fsS "${CP_URL}/admin/control-plane/fleet/status" | jq .

echo "==> Data path through Envoy -> Runtime"
curl -fsS "${ENVOY_URL}/api/v1/small" | jq .

echo "==> Restart durability check"
"${BYTETAPER_COMPOSE[@]}" restart bytetaper-control-plane bytetaper-runtime
poll_control_plane_health
poll_runtime_ready
curl -fsS "${CP_URL}/admin/control-plane/policy/current" | jq .
curl -fsS "${ENVOY_URL}/api/v1/small" | jq .

echo "PASS: control-plane demo smoke completed"

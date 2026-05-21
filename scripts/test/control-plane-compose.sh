#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
# Control Plane Compose regression smoke (success + failure flows).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=scripts/test/_control_plane_compose_lib.sh
source "${ROOT}/scripts/test/_control_plane_compose_lib.sh"

bytetaper_compose_init

export BYTETAPER_RUNTIME_METRICS_URL="${BYTETAPER_RUNTIME_METRICS_URL:-http://127.0.0.1:18083}"

CP_URL="$(cp_url)"
ENVOY_URL="$(envoy_url)"
APPLY_YAML="${ROOT}/tests/fixtures/control-plane/apply-policy-source.yaml"
FIELD_E2E="${ROOT}/tests/integration/e2e/control_plane_field_allowlist_e2e_test.py"

trap 'cp_teardown_volumes' EXIT

echo "==> Success flow: start profile and poll health"
cp_start_profile

echo "==> policy/current (bootstrap generation 1 when store empty)"
curl -fsS "${CP_URL}/admin/control-plane/policy/current" | jq .

echo "==> Apply policy via Control Plane (CAS from current)"
cp_apply_policy_yaml "${APPLY_YAML}" "cp-compose-apply-1" "control-plane-compose" | jq .

echo "==> Wait for applied generation on control plane"
poll_policy_current_generation 2 90

echo "==> Poll fleet convergence after apply (no fixed sleep)"
poll_fleet_converged 120

echo "==> Fleet status"
curl -fsS "${CP_URL}/admin/control-plane/fleet/status" | jq .

echo "==> Data path through Envoy"
curl -fsS "${ENVOY_URL}/api/v1/small" | jq .

echo "==> Restart durability (CP + runtime)"
"${BYTETAPER_COMPOSE[@]}" restart bytetaper-control-plane bytetaper-runtime
poll_control_plane_health
poll_runtime_compose_healthy 120
poll_runtime_ready 30
poll_envoy_data_path

echo "==> Re-verify strict fleet convergence after restart"
poll_fleet_converged 120
FLEET_AFTER_RESTART="$(fetch_fleet_status)"
assert_fleet_strictly_converged "${FLEET_AFTER_RESTART}" "post-restart fleet"

GEN_AFTER_RESTART="$(echo "${FLEET_AFTER_RESTART}" | jq -r '.committed.generation')"
POLICY_ID_AFTER_RESTART="$(echo "${FLEET_AFTER_RESTART}" | jq -r '.committed.policy_id')"
HASH_AFTER_RESTART="$(echo "${FLEET_AFTER_RESTART}" | jq -r '.committed.canonical_hash')"
if [[ "${GEN_AFTER_RESTART}" == "0" || "${GEN_AFTER_RESTART}" == "null" ]]; then
  echo "policy generation missing after restart" >&2
  exit 1
fi
curl -fsS "${CP_URL}/admin/control-plane/policy/current" | jq .
curl -fsS "${ENVOY_URL}/api/v1/small" | jq .

echo "==> Failure flow: Control Plane stop (runtime LKG via Envoy)"
GEN_BEFORE_STOP="${GEN_AFTER_RESTART}"
POLICY_ID_BEFORE_STOP="${POLICY_ID_AFTER_RESTART}"
HASH_BEFORE_STOP="${HASH_AFTER_RESTART}"
"${BYTETAPER_COMPOSE[@]}" stop bytetaper-control-plane
poll_envoy_data_path 60
curl -fsS "${ENVOY_URL}/api/v1/small" | jq .
echo "PASS: Envoy still serves while control plane is stopped"

echo "==> Restart Control Plane and verify runtime reconnect + fleet convergence"
"${BYTETAPER_COMPOSE[@]}" start bytetaper-control-plane
poll_control_plane_health
poll_fleet_converged 120
FLEET_RECONNECTED="$(fetch_fleet_status)"
assert_fleet_strictly_converged "${FLEET_RECONNECTED}" "post-cp-restart fleet"

echo "==> Tamper runtime mirror to stale local generation (self-consistent YAML + metadata)"
STALE_POLICY_ID="$(curl -fsS \
  "${CP_URL}/admin/control-plane/policy/version?resource_key=policy/default/runtime&generation=$((GEN_BEFORE_STOP - 1))" \
  | jq -r '.policy_id')"
tamper_runtime_mirror_stale_local "${GEN_BEFORE_STOP}" "${POLICY_ID_BEFORE_STOP}"
assert_no_committed_adopt_of_stale_mirror \
  "${GEN_BEFORE_STOP}" "${POLICY_ID_BEFORE_STOP}" "${HASH_BEFORE_STOP}" "${STALE_POLICY_ID}"

echo "==> Restart runtime; local mirror tamper must not change CP committed pointer"
"${BYTETAPER_COMPOSE[@]}" restart bytetaper-runtime
poll_runtime_compose_healthy 120
poll_runtime_ready 30
poll_fleet_converged 120
assert_no_committed_adopt_of_stale_mirror \
  "${GEN_BEFORE_STOP}" "${POLICY_ID_BEFORE_STOP}" "${HASH_BEFORE_STOP}" "${STALE_POLICY_ID}"
echo "PASS: runtime healthy after mirror tamper; CP kept committed active pointer"
echo "NOTE: explicit repair_local/adopt_local is out of script scope; see manual_resolution_integration_test"

echo "==> Restart runtime before field-allowlist E2E (clear in-process cache)"
"${BYTETAPER_COMPOSE[@]}" restart bytetaper-runtime
poll_runtime_compose_healthy 120
poll_runtime_ready 30
poll_fleet_converged 120

echo "==> Field-allowlist E2E (CP policy + Envoy data path)"
if [[ ! -f "${FIELD_E2E}" ]]; then
  echo "missing field-allowlist E2E script: ${FIELD_E2E}" >&2
  exit 1
fi
BYTETAPER_CONTROL_PLANE_URL="${CP_URL}" \
BYTETAPER_ENVOY_URL="${ENVOY_URL}" \
BYTETAPER_FIXTURES_DIR="${ROOT}/tests/fixtures/control-plane" \
BYTETAPER_FIELD_E2E_SKIP_OPEN_APPLY=1 \
  python3 "${FIELD_E2E}"

trap - EXIT
cp_teardown_volumes
echo "PASS: control-plane compose regression smoke completed"

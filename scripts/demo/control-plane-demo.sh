#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

if docker compose version >/dev/null 2>&1; then
  COMPOSE=(docker compose -f docker-compose.yml -f docker-compose.control-plane.yml)
elif command -v docker-compose >/dev/null 2>&1; then
  COMPOSE=(docker-compose -f docker-compose.yml -f docker-compose.control-plane.yml)
else
  echo "docker compose or docker-compose is required" >&2
  exit 1
fi

CP_URL="${BYTETAPER_CONTROL_PLANE_URL:-http://127.0.0.1:19090}"
ENVOY_URL="${BYTETAPER_ENVOY_URL:-http://127.0.0.1:10000}"

echo "==> Starting Control Plane + Runtime profile"
LOCAL_UID="$(id -u)" LOCAL_GID="$(id -g)" "${COMPOSE[@]}" up -d \
  bytetaper-control-plane bytetaper-runtime mock-api envoy

echo "==> Waiting for Control Plane health"
for _ in $(seq 1 60); do
  if curl -fsS "${CP_URL}/healthz" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done
curl -fsS "${CP_URL}/healthz" >/dev/null

echo "==> Control Plane policy/current (bootstrap seeds generation 1 when store is empty)"
curl -fsS "${CP_URL}/admin/control-plane/policy/current" | jq .

echo "==> Applying policy through Control Plane (CAS from current committed pointer)"
CURRENT_JSON="$(curl -fsS "${CP_URL}/admin/control-plane/policy/current")"
BASE_GENERATION="$(echo "${CURRENT_JSON}" | jq -r '.generation // 0')"
BASE_POLICY_ID="$(echo "${CURRENT_JSON}" | jq -r '.policy_id // ""')"
if [[ "${BASE_GENERATION}" == "0" || -z "${BASE_POLICY_ID}" ]]; then
  echo "no committed policy to apply against (bootstrap may have failed)" >&2
  exit 1
fi
# Candidate must keep cache.enabled=true when field_variant is enabled (see FVC_WITHOUT_CACHE).
APPLY_PAYLOAD="$(jq -n \
  --rawfile source "${ROOT}/examples/taperquery/apply-policy-source.yaml" \
  --arg request_id "cp-demo-apply-1" \
  --arg operator_id "control-plane-demo" \
  --argjson expected_base_generation "${BASE_GENERATION}" \
  --arg expected_base_policy_id "${BASE_POLICY_ID}" \
  '{source: $source, request_id: $request_id, operator_id: $operator_id,
    expected_base_generation: $expected_base_generation,
    expected_base_policy_id: $expected_base_policy_id}')"
curl -fsS -X POST "${CP_URL}/admin/control-plane/policy/apply" \
  -H 'content-type: application/json' \
  -d "${APPLY_PAYLOAD}" | jq .

echo "==> Waiting for runtime pull/convergence"
sleep 5

echo "==> Fleet status"
curl -fsS "${CP_URL}/admin/control-plane/fleet/status" | jq .

echo "==> Data path through Envoy -> Runtime"
curl -fsS "${ENVOY_URL}/api/v1/small" | jq .

echo "==> Restart durability check"
"${COMPOSE[@]}" restart bytetaper-control-plane bytetaper-runtime
sleep 8
curl -fsS "${CP_URL}/admin/control-plane/policy/current" | jq .
curl -fsS "${ENVOY_URL}/api/v1/small" | jq .

echo "PASS: control-plane demo smoke completed"

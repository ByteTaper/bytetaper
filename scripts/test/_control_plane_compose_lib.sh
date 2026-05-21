#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
# Shared helpers for Control Plane Compose smoke.

set -euo pipefail

ROOT="${BYTETAPER_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"

# Sets BYTETAPER_COMPOSE_CMD to the validated docker compose CLI (no -f flags).
bytetaper_resolve_compose_cmd() {
  local -a compose_cmd=()
  local docker_compose_known=0
  if [[ -n "${DOCKER_COMPOSE:-}" ]]; then
    case "${DOCKER_COMPOSE}" in
      "docker compose")
        docker_compose_known=1
        compose_cmd=(docker compose) ;;
      docker-compose)
        docker_compose_known=1
        compose_cmd=(docker-compose) ;;
      *)
        # shellcheck disable=SC2206
        compose_cmd=(${DOCKER_COMPOSE}) ;;
    esac
    if [[ ${#compose_cmd[@]} -gt 0 ]] && "${compose_cmd[@]}" version >/dev/null 2>&1; then
      BYTETAPER_COMPOSE_CMD=("${compose_cmd[@]}")
      return 0
    fi
    if [[ "${docker_compose_known}" -eq 0 ]]; then
      echo "Ignoring invalid DOCKER_COMPOSE='${DOCKER_COMPOSE}'" >&2
    fi
    compose_cmd=()
  fi
  if command -v docker-compose >/dev/null 2>&1 && docker-compose version >/dev/null 2>&1; then
    BYTETAPER_COMPOSE_CMD=(docker-compose)
    return 0
  fi
  if command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
    BYTETAPER_COMPOSE_CMD=(docker compose)
    return 0
  fi
  echo "docker compose or docker-compose is required" >&2
  return 1
}

bytetaper_compose_init() {
  cd "$ROOT"
  bytetaper_resolve_compose_cmd
  BYTETAPER_COMPOSE=(
    "${BYTETAPER_COMPOSE_CMD[@]}"
    -f docker-compose.yml
    -f docker-compose.control-plane.yml
  )
  export LOCAL_UID="${LOCAL_UID:-$(id -u)}"
  export LOCAL_GID="${LOCAL_GID:-$(id -g)}"
}

cp_compose_diagnostics() {
  local tail_lines="${BYTETAPER_CP_COMPOSE_LOG_TAIL:-120}"
  local service
  echo "==> Compose diagnostics: ps" >&2
  "${BYTETAPER_COMPOSE[@]}" ps >&2 || true
  for service in bytetaper-build-server bytetaper-control-plane bytetaper-runtime envoy mock-api; do
    echo "==> Compose diagnostics: ${service} logs (tail ${tail_lines})" >&2
    "${BYTETAPER_COMPOSE[@]}" logs --no-color --tail="${tail_lines}" "${service}" >&2 || true
  done
}

cp_url() {
  echo "${BYTETAPER_CONTROL_PLANE_URL:-http://127.0.0.1:19090}"
}

runtime_metrics_url() {
  echo "${BYTETAPER_RUNTIME_METRICS_URL:-http://127.0.0.1:18083}"
}

envoy_url() {
  echo "${BYTETAPER_ENVOY_URL:-http://127.0.0.1:10000}"
}

poll_http_ok() {
  local url="$1"
  local label="${2:-endpoint}"
  local attempts="${3:-60}"
  local i
  for ((i = 1; i <= attempts; i++)); do
    if curl -fsS "${url}" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  echo "timeout waiting for ${label}: ${url}" >&2
  return 1
}

poll_control_plane_health() {
  poll_http_ok "$(cp_url)/healthz" "control plane health"
}

runtime_ready_probe_inside_container() {
  "${BYTETAPER_COMPOSE[@]}" exec -T bytetaper-runtime bash -lc \
    'exec 3<>/dev/tcp/127.0.0.1/18081 && printf "GET /readyz HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" >&3 && read -r line <&3 && [[ "$line" == *"200"* ]]'
}

# Probe /readyz inside bytetaper-runtime (authoritative). Host :18081 may map to
# monolithic bytetaper-extproc when both are started for Envoy.
poll_runtime_compose_healthy() {
  local attempts="${1:-90}"
  local i health
  for ((i = 1; i <= attempts; i++)); do
    health="$("${BYTETAPER_COMPOSE[@]}" ps bytetaper-runtime --format json 2>/dev/null \
      | jq -r 'if type == "array" then .[0].Health else .Health end // ""' 2>/dev/null || true)"
    if [[ "${health}" == "healthy" ]]; then
      return 0
    fi
    sleep 1
  done
  echo "timeout waiting for bytetaper-runtime compose health" >&2
  "${BYTETAPER_COMPOSE[@]}" ps bytetaper-runtime 2>&1 || true
  return 1
}

poll_runtime_ready() {
  local attempts="${1:-60}"
  local i
  poll_runtime_compose_healthy "${attempts}" || return 1
  for ((i = 1; i <= attempts; i++)); do
    if runtime_ready_probe_inside_container; then
      return 0
    fi
    if [[ -n "${BYTETAPER_RUNTIME_READY_VIA_HOST:-}" ]]; then
      if poll_http_ok "$(runtime_metrics_url)/readyz" "runtime ready (host)" 1; then
        return 0
      fi
    fi
    sleep 1
  done
  echo "timeout waiting for runtime ready (bytetaper-runtime container /readyz)" >&2
  return 1
}

poll_envoy_data_path() {
  poll_http_ok "$(envoy_url)/api/v1/small" "envoy data path"
}

wait_for_build_server_completed() {
  local timeout="${BYTETAPER_CP_COMPOSE_BUILD_TIMEOUT:-600}"
  local i state exit_code status ps_json

  echo "==> Waiting for bytetaper-build-server (timeout ${timeout}s)"

  if command -v timeout >/dev/null 2>&1 &&
    "${BYTETAPER_COMPOSE[@]}" wait --help >/dev/null 2>&1; then
    if timeout "${timeout}" "${BYTETAPER_COMPOSE[@]}" wait bytetaper-build-server; then
      echo "==> bytetaper-build-server completed"
      return 0
    fi
    local wait_rc=$?
    if [[ "${wait_rc}" -eq 124 ]]; then
      echo "timeout waiting for bytetaper-build-server after ${timeout}s" >&2
      cp_compose_diagnostics
      return 1
    fi
    echo "bytetaper-build-server wait failed (exit ${wait_rc})" >&2
    cp_compose_diagnostics
    return 1
  fi

  for ((i = 1; i <= timeout; i++)); do
    ps_json="$("${BYTETAPER_COMPOSE[@]}" ps bytetaper-build-server --format json 2>/dev/null || true)"
    state="$(echo "${ps_json}" | jq -r 'if type == "array" then .[0].State else .State end // ""' 2>/dev/null || true)"
    exit_code="$(echo "${ps_json}" | jq -r 'if type == "array" then .[0].ExitCode else .ExitCode end // ""' 2>/dev/null || true)"
    status="$(echo "${ps_json}" | jq -r 'if type == "array" then .[0].Status else .Status end // ""' 2>/dev/null || true)"

    if [[ "${state}" == "exited" && "${exit_code}" == "0" ]]; then
      return 0
    fi
    if [[ "${state}" == "exited" && -n "${exit_code}" && "${exit_code}" != "0" ]]; then
      echo "bytetaper-build-server exited with ${exit_code}" >&2
      cp_compose_diagnostics
      return 1
    fi
    if [[ -z "${exit_code}" && "${status}" == *"Exited (0)"* ]]; then
      return 0
    fi
    if [[ "${status}" == *"Exited ("* && "${status}" != *"Exited (0)"* ]]; then
      echo "bytetaper-build-server failed: ${status}" >&2
      cp_compose_diagnostics
      return 1
    fi
    if ((i % 30 == 0)); then
      echo "==> still waiting for bytetaper-build-server (${i}s, state=${state:-unknown})" >&2
    fi
    sleep 1
  done
  echo "timeout waiting for bytetaper-build-server to complete after ${timeout}s" >&2
  cp_compose_diagnostics
  return 1
}

fetch_fleet_status() {
  curl -fsS "$(cp_url)/admin/control-plane/fleet/status"
}

poll_policy_current_generation() {
  local expected_generation="$1"
  local attempts="${2:-60}"
  local cp="$(cp_url)"
  local i current_gen
  for ((i = 1; i <= attempts; i++)); do
    current_gen="$(curl -fsS "${cp}/admin/control-plane/policy/current" | jq -r '.generation // 0')"
    if [[ "${current_gen}" == "${expected_generation}" ]]; then
      return 0
    fi
    sleep 1
  done
  echo "timeout waiting for policy/current generation ${expected_generation} (last=${current_gen})" >&2
  return 1
}

# require aggregate fleet.converged and per-runtime hash/generation match.
assert_fleet_strictly_converged() {
  local fleet_json="$1"
  local label="${2:-fleet}"

  if ! echo "${fleet_json}" | jq -e '.ok == true' >/dev/null 2>&1; then
    echo "${label}: fleet status not ok" >&2
    echo "${fleet_json}" | jq . >&2 || echo "${fleet_json}" >&2
    return 1
  fi
  if ! echo "${fleet_json}" | jq -e '.fleet.converged == true' >/dev/null 2>&1; then
    echo "${label}: fleet.converged is not true" >&2
    echo "${fleet_json}" | jq . >&2
    return 1
  fi

  local runtime_count converged_count stale_count committed_gen committed_policy committed_hash
  runtime_count="$(echo "${fleet_json}" | jq -r '.fleet.runtime_count // 0')"
  converged_count="$(echo "${fleet_json}" | jq -r '.fleet.converged_count // 0')"
  stale_count="$(echo "${fleet_json}" | jq -r '.fleet.stale_count // 0')"
  committed_gen="$(echo "${fleet_json}" | jq -r '.committed.generation // 0')"
  committed_policy="$(echo "${fleet_json}" | jq -r '.committed.policy_id // ""')"
  committed_hash="$(echo "${fleet_json}" | jq -r '.committed.canonical_hash // ""')"

  if [[ "${runtime_count}" == "0" ]]; then
    echo "${label}: expected at least one runtime report" >&2
    return 1
  fi
  if [[ "${converged_count}" != "${runtime_count}" ]]; then
    echo "${label}: converged_count (${converged_count}) != runtime_count (${runtime_count})" >&2
    return 1
  fi
  if [[ "${stale_count}" != "0" ]]; then
    echo "${label}: stale_count must be 0 (got ${stale_count})" >&2
    return 1
  fi

  if ! echo "${fleet_json}" | jq -e --argjson gen "${committed_gen}" \
      --arg policy "${committed_policy}" --arg hash "${committed_hash}" '
    (.runtimes | length) > 0 and
    all(.runtimes[];
      .control_plane_reachable == true and
      .convergence_status == "converged" and
      .active_generation == $gen and
      .active_policy_id == $policy and
      .active_canonical_hash == $hash)
  ' >/dev/null 2>&1; then
    echo "${label}: runtime entries must be reachable, converged, and match committed pointer" >&2
    echo "${fleet_json}" | jq '.runtimes' >&2
    return 1
  fi
  return 0
}

poll_fleet_converged() {
  local attempts="${1:-90}"
  local i fleet_json
  for ((i = 1; i <= attempts; i++)); do
    if fleet_json="$(fetch_fleet_status 2>/dev/null)"; then
      if echo "${fleet_json}" | jq -e '.fleet.converged == true' >/dev/null 2>&1; then
        assert_fleet_strictly_converged "${fleet_json}" "poll_fleet_converged"
        return 0
      fi
    fi
    sleep 1
  done
  echo "timeout waiting for strict fleet convergence" >&2
  fetch_fleet_status | jq . >&2 || true
  return 1
}

# After mirror tamper + runtime restart, observe fleet mismatch before CP pull reconverges.
poll_fleet_mismatch_after_tamper() {
  local attempts="${1:-60}"
  local expected_generation="$2"
  local expected_policy_id="$3"
  local expected_hash="$4"
  local i fleet_json
  for ((i = 1; i <= attempts; i++)); do
    if fleet_json="$(fetch_fleet_status 2>/dev/null)"; then
      if echo "${fleet_json}" | jq -e '.fleet.converged == false' >/dev/null 2>&1; then
        assert_runtime_reports_manual_resolution_state "${fleet_json}" "post-tamper fleet"
        assert_committed_policy_unchanged "${expected_generation}" "${expected_policy_id}" "${expected_hash}"
        return 0
      fi
      if echo "${fleet_json}" | jq -e --argjson gen "${expected_generation}" --arg policy "${expected_policy_id}" '
          .runtimes[]? |
          select(.active_generation < $gen or .active_policy_id != $policy or .convergence_status == "stale")
        ' >/dev/null 2>&1; then
        assert_committed_policy_unchanged "${expected_generation}" "${expected_policy_id}" "${expected_hash}"
        return 0
      fi
      if echo "${fleet_json}" | jq -e --arg policy "${expected_policy_id}" '
          .runtimes[]? |
          select(.convergence_status == "divergent" and .active_policy_id != $policy)
        ' >/dev/null 2>&1; then
        assert_committed_policy_unchanged "${expected_generation}" "${expected_policy_id}" "${expected_hash}"
        return 0
      fi
    fi
    sleep 0.5
  done
  echo "timeout waiting for fleet mismatch after mirror tamper" >&2
  fetch_fleet_status | jq . >&2 || true
  return 1
}

assert_runtime_reports_manual_resolution_state() {
  local fleet_json="$1"
  local label="${2:-post-tamper fleet}"

  if ! echo "${fleet_json}" | jq -e '
    .runtimes[]? |
    select(
      .convergence_status == "stale" or
      .convergence_status == "divergent" or
      .convergence_status == "failed" or
      (.activation_status | test("inactive|corrupt|mismatch|failed|unparseable"; "i"))
    )
  ' >/dev/null 2>&1; then
    echo "${label}: expected stale/divergent/failed or manual-resolution activation_status" >&2
    echo "${fleet_json}" | jq '.runtimes' >&2
    return 1
  fi
  return 0
}

assert_committed_policy_unchanged() {
  local expected_generation="$1"
  local expected_policy_id="$2"
  local expected_hash="${3:-}"
  local current_json
  current_json="$(curl -fsS "$(cp_url)/admin/control-plane/policy/current")"

  local generation policy_id
  generation="$(echo "${current_json}" | jq -r '.generation // 0')"
  policy_id="$(echo "${current_json}" | jq -r '.policy_id // ""')"

  if [[ "${generation}" != "${expected_generation}" || "${policy_id}" != "${expected_policy_id}" ]]; then
    echo "committed policy pointer changed (expected gen=${expected_generation} id=${expected_policy_id}, got gen=${generation} id=${policy_id})" >&2
    return 1
  fi

  if [[ -n "${expected_hash}" ]]; then
    local fleet_json committed_hash
    fleet_json="$(fetch_fleet_status)"
    committed_hash="$(echo "${fleet_json}" | jq -r '.committed.canonical_hash // ""')"
    if [[ "${committed_hash}" != "${expected_hash}" ]]; then
      echo "committed canonical_hash changed (expected ${expected_hash}, got ${committed_hash})" >&2
      return 1
    fi
  fi
  return 0
}

# Replace mirror with a self-consistent *stale* committed generation (G-1) from CP.
# YAML and metadata must agree or StartupPolicyLoader fails closed before fleet reporting.
tamper_runtime_mirror_stale_local() {
  local committed_generation="$1"
  local committed_policy_id="$2"

  if [[ "${committed_generation}" -le 1 ]]; then
    echo "stale mirror tamper requires committed generation > 1 (got ${committed_generation})" >&2
    return 1
  fi

  local stale_generation=$((committed_generation - 1))
  local cp="$(cp_url)"
  local stale_policy_id
  stale_policy_id="$("${BYTETAPER_COMPOSE[@]}" exec -T bytetaper-runtime bash -lc \
    "curl -fsS 'http://bytetaper-control-plane:19090/admin/control-plane/policy/version?resource_key=policy/default/runtime&generation=${stale_generation}'" \
    | jq -r '.policy_id')"
  if [[ -z "${stale_policy_id}" || "${stale_policy_id}" == "null" ]]; then
    echo "failed to fetch policy version ${stale_generation} for stale mirror tamper" >&2
    return 1
  fi

  "${BYTETAPER_COMPOSE[@]}" exec -T --user 0 bytetaper-runtime python3 - "${stale_generation}" <<'PY'
import json
import os
import subprocess
import sys
import time

stale_generation = int(sys.argv[1])
version_raw = subprocess.check_output(
    [
        "curl",
        "-fsS",
        f"http://bytetaper-control-plane:19090/admin/control-plane/policy/version"
        f"?resource_key=policy/default/runtime&generation={stale_generation}",
    ],
    text=True,
)
version = json.loads(version_raw)
import json
import os
import sys
import time

if not version.get("ok"):
    raise SystemExit("version payload not ok")

policy_dir = "/var/lib/bytetaper/policies"
os.makedirs(policy_dir, exist_ok=True)
yaml_path = f"{policy_dir}/active-policy.yaml"
meta_path = f"{policy_dir}/active-policy.meta.json"

yaml_text = version["canonical_yaml"]
if not yaml_text.endswith("\n"):
    yaml_text += "\n"

generation = int(version["generation"])
policy_id = version["policy_id"]
canonical_hash = version["canonical_hash"]

if generation != stale_generation:
    raise SystemExit(f"stale generation mismatch: meta={generation} expected={stale_generation}")


def make_versioned_rel(gen: int, identity: str) -> str:
    id_part = identity[7:] if identity.startswith("sha256:") else identity
    id_part = id_part[:8]
    return f"versions/{gen:010d}-sha256_{id_part}.yaml"


versioned_rel = make_versioned_rel(generation, policy_id)
versioned_path = os.path.join(policy_dir, versioned_rel)
os.makedirs(os.path.dirname(versioned_path), exist_ok=True)
with open(versioned_path, "w", encoding="utf-8") as handle:
    handle.write(yaml_text)

with open(yaml_path, "w", encoding="utf-8") as handle:
    handle.write(yaml_text)

digest = canonical_hash[7:] if canonical_hash.startswith("sha256:") else canonical_hash
if os.path.isfile(meta_path):
    with open(meta_path, encoding="utf-8") as handle:
        meta = json.load(handle)
    if "metadataSchemaVersion" in meta:
        meta["generation"] = generation
        meta["policyId"] = policy_id
        meta["canonicalHash"] = canonical_hash
        meta["versionedPolicyFile"] = versioned_rel
    else:
        meta["generation"] = generation
        meta["policy_identity"] = policy_id
        meta["canonical_hash"] = canonical_hash
        meta["canonical_yaml_sha256"] = digest
        meta["versioned_policy_file"] = versioned_rel
else:
    meta = {
        "metadata_schema_version": 1,
        "resource_key": "policy/default/runtime",
        "generation": generation,
        "policy_identity": policy_id,
        "canonical_hash": canonical_hash,
        "canonical_hash_algorithm": "sha256",
        "canonical_yaml_sha256": digest,
        "source_type": "manual-repair",
        "schema_version": 1,
        "api_version": "bytetaper.io/v1alpha1",
        "kind": "RuntimePolicy",
        "active_policy_file": "active-policy.yaml",
        "versioned_policy_file": versioned_rel,
        "written_at_unix_epoch_ms": int(time.time() * 1000),
    }

with open(meta_path, "w", encoding="utf-8") as handle:
    json.dump(meta, handle)
    handle.write("\n")
PY

  assert_runtime_mirror_stale_local "${committed_generation}" "${committed_policy_id}" "${stale_policy_id}"
}

assert_runtime_mirror_stale_local() {
  local committed_generation="$1"
  local committed_policy_id="$2"
  local stale_policy_id="$3"
  local stale_generation=$((committed_generation - 1))

  local meta_generation meta_policy_id
  meta_generation="$("${BYTETAPER_COMPOSE[@]}" exec -T bytetaper-runtime bash -lc \
    "python3 - <<'PY'
import json
with open('/var/lib/bytetaper/policies/active-policy.meta.json', encoding='utf-8') as f:
    meta = json.load(f)
print(meta.get('generation') or 0)
PY")"
  meta_generation="${meta_generation//$'\r'/}"

  meta_policy_id="$("${BYTETAPER_COMPOSE[@]}" exec -T bytetaper-runtime bash -lc \
    "python3 - <<'PY'
import json
with open('/var/lib/bytetaper/policies/active-policy.meta.json', encoding='utf-8') as f:
    meta = json.load(f)
print(meta.get('policyId') or meta.get('policy_identity') or '')
PY")"
  meta_policy_id="${meta_policy_id//$'\r'/}"

  if [[ "${meta_generation}" != "${stale_generation}" ]]; then
    echo "expected stale mirror generation ${stale_generation}, got ${meta_generation}" >&2
    return 1
  fi
  if [[ "${meta_policy_id}" != "${stale_policy_id}" ]]; then
    echo "expected stale mirror policy id ${stale_policy_id}, got ${meta_policy_id}" >&2
    return 1
  fi
  if [[ "${meta_policy_id}" == "${committed_policy_id}" && "${stale_generation}" == "${committed_generation}" ]]; then
    echo "stale mirror must differ from committed active policy" >&2
    return 1
  fi

  "${BYTETAPER_COMPOSE[@]}" exec -T bytetaper-runtime bash -lc \
    "python3 - \"${stale_generation}\" \"${stale_policy_id}\" <<'PY'
import json
import os
import sys

stale_generation = int(sys.argv[1])
stale_policy_id = sys.argv[2]
policy_dir = '/var/lib/bytetaper/policies'
meta_path = f'{policy_dir}/active-policy.meta.json'
with open(meta_path, encoding='utf-8') as handle:
    meta = json.load(handle)
versioned_rel = meta.get('versionedPolicyFile') or meta.get('versioned_policy_file') or ''
if not versioned_rel:
    raise SystemExit('versioned policy file missing from metadata')
versioned_path = os.path.join(policy_dir, versioned_rel)
if not os.path.isfile(versioned_path):
    raise SystemExit(f'versioned policy file missing: {versioned_path}')
id_part = stale_policy_id[7:] if stale_policy_id.startswith('sha256:') else stale_policy_id
expected_rel = f'versions/{stale_generation:010d}-sha256_{id_part[:8]}.yaml'
if versioned_rel != expected_rel:
    raise SystemExit(f'versioned path mismatch: {versioned_rel} != {expected_rel}')
PY"
}

assert_no_committed_adopt_of_stale_mirror() {
  local expected_generation="$1"
  local expected_policy_id="$2"
  local expected_hash="${3:-}"
  local stale_policy_id="$4"

  assert_committed_policy_unchanged "${expected_generation}" "${expected_policy_id}" "${expected_hash}"

  local current_policy_id
  current_policy_id="$(curl -fsS "$(cp_url)/admin/control-plane/policy/current" | jq -r '.policy_id // ""')"
  if [[ "${current_policy_id}" == "${stale_policy_id}" ]]; then
    echo "control plane adopted stale mirror policy id ${stale_policy_id}" >&2
    return 1
  fi
  if [[ "${current_policy_id}" != "${expected_policy_id}" ]]; then
    echo "committed active policy id changed unexpectedly" >&2
    return 1
  fi
}

cp_apply_policy_yaml() {
  local yaml_path="$1"
  local request_id="${2:-cp-compose-apply}"
  local operator_id="${3:-control-plane-compose}"
  local cp="$(cp_url)"

  local current_json base_generation base_policy_id
  current_json="$(curl -fsS "${cp}/admin/control-plane/policy/current")"
  base_generation="$(echo "${current_json}" | jq -r '.generation // 0')"
  base_policy_id="$(echo "${current_json}" | jq -r '.policy_id // ""')"
  if [[ "${base_generation}" == "0" || -z "${base_policy_id}" ]]; then
    echo "no committed policy to apply against" >&2
    return 1
  fi

  local payload
  payload="$(jq -n \
    --rawfile source "${yaml_path}" \
    --arg request_id "${request_id}" \
    --arg operator_id "${operator_id}" \
    --argjson expected_base_generation "${base_generation}" \
    --arg expected_base_policy_id "${base_policy_id}" \
    '{source: $source, request_id: $request_id, operator_id: $operator_id,
      expected_base_generation: $expected_base_generation,
      expected_base_policy_id: $expected_base_policy_id}')"

  curl -fsS -X POST "${cp}/admin/control-plane/policy/apply" \
    -H 'content-type: application/json' \
    -d "${payload}"
}

cp_compose_service_running() {
  local service="$1"
  local state
  state="$("${BYTETAPER_COMPOSE[@]}" ps "${service}" --format json 2>/dev/null \
    | jq -r 'if type == "array" then .[0].State else .State end // ""' 2>/dev/null || true)"
  [[ "${state}" == "running" ]]
}

cp_profile_is_ready() {
  local service
  for service in mock-api bytetaper-control-plane bytetaper-runtime envoy; do
    if ! cp_compose_service_running "${service}"; then
      return 1
    fi
  done
  poll_http_ok "$(cp_url)/healthz" "control plane health" 5 || return 1
  poll_runtime_ready 15 || return 1
  poll_http_ok "$(envoy_url)/api/v1/small" "envoy data path" 5 || return 1
}

cp_start_profile() {
  echo "==> Starting Control Plane + Runtime profile"
  if [[ "${BYTETAPER_CP_COMPOSE_REUSE_READY:-0}" == "1" ]] && cp_profile_is_ready; then
    echo "==> Reusing healthy Control Plane profile (skip clean/build/startup)"
    poll_control_plane_health
    poll_runtime_ready 30
    poll_envoy_data_path
    return 0
  fi
  if [[ "${BYTETAPER_CP_COMPOSE_CLEAN:-1}" == "1" ]]; then
    echo "==> Cleaning stale profile state"
    "${BYTETAPER_COMPOSE[@]}" down -v --remove-orphans
  fi
  echo "==> Starting mock-api"
  "${BYTETAPER_COMPOSE[@]}" up -d mock-api
  echo "==> Building bytetaper-extproc-server (compose run --rm bytetaper-build-server)"
  if ! "${BYTETAPER_COMPOSE[@]}" run --rm bytetaper-build-server; then
    echo "bytetaper-build-server failed" >&2
    cp_compose_diagnostics
    return 1
  fi
  echo "==> Starting bytetaper-control-plane, bytetaper-runtime, and envoy"
  "${BYTETAPER_COMPOSE[@]}" up -d bytetaper-control-plane bytetaper-runtime envoy
  poll_control_plane_health
  poll_runtime_ready 30
  poll_envoy_data_path
}

cp_teardown_volumes() {
  echo "==> Tearing down profile and volumes"
  "${BYTETAPER_COMPOSE[@]}" down -v --remove-orphans
}

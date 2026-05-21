#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
"""Apply policy via Control Plane admin HTTP; verify field filtering on Envoy."""

import json
import os
import sys
import time
import urllib.error
import urllib.request

CP_URL = os.environ.get("BYTETAPER_CONTROL_PLANE_URL", "http://bytetaper-control-plane:19090")
ENVOY_URL = os.environ.get("BYTETAPER_ENVOY_URL", "http://envoy:10000")
FIXTURES = os.environ.get(
    "BYTETAPER_FIXTURES_DIR",
    os.path.join(os.path.dirname(__file__), "..", "..", "fixtures", "control-plane"),
)

SENSITIVE_FIELDS = ["secret_token", "debug_internal", "request_path", "generated_at_epoch_ms"]
ALLOWED_AFTER_REDUCTION = ["service", "scenario", "version", "payload", "sentinel"]
SKIP_OPEN_APPLY = os.environ.get("BYTETAPER_FIELD_E2E_SKIP_OPEN_APPLY", "0") == "1"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def get(url: str) -> tuple[int, str]:
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=30.0) as resp:
            return resp.getcode(), resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8")
    except Exception as e:
        return -1, str(e)


def post_json(url: str, payload: dict) -> tuple[int, str]:
    try:
        body = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            url, data=body, headers={"Content-Type": "application/json"}, method="POST"
        )
        with urllib.request.urlopen(req, timeout=120.0) as resp:
            return resp.getcode(), resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8")
    except Exception as e:
        return -1, str(e)


def read_yaml(name: str) -> str:
    path = os.path.join(FIXTURES, name)
    with open(path, encoding="utf-8") as f:
        return f.read()


def poll_fleet_converged(timeout_s: float = 120.0) -> None:
    deadline = time.time() + timeout_s
    last_body = ""
    while time.time() < deadline:
        code, body = get(f"{CP_URL}/admin/control-plane/fleet/status")
        last_body = body
        if code != 200:
            time.sleep(1)
            continue
        data = json.loads(body)
        fleet = data.get("fleet") or {}
        committed = data.get("committed") or {}
        runtimes = data.get("runtimes") or []
        if fleet.get("converged") is not True:
            time.sleep(1)
            continue
        if fleet.get("runtime_count", 0) == 0:
            time.sleep(1)
            continue
        if fleet.get("converged_count") != fleet.get("runtime_count"):
            time.sleep(1)
            continue
        if fleet.get("stale_count", 0) != 0:
            time.sleep(1)
            continue
        committed_gen = committed.get("generation")
        committed_policy = committed.get("policy_id")
        committed_hash = committed.get("canonical_hash")
        all_match = all(
            rt.get("control_plane_reachable") is True
            and rt.get("convergence_status") == "converged"
            and rt.get("active_generation") == committed_gen
            and rt.get("active_policy_id") == committed_policy
            and rt.get("active_canonical_hash") == committed_hash
            for rt in runtimes
        )
        if all_match:
            return
        time.sleep(1)
    fail(f"fleet did not strictly converge within {timeout_s}s: last body={last_body}")


def apply_policy_yaml(yaml_name: str, request_id: str, *, allow_no_changes: bool = False) -> None:
    code, body = get(f"{CP_URL}/admin/control-plane/policy/current")
    if code != 200:
        fail(f"policy/current failed ({code}): {body}")
    current = json.loads(body)
    base_generation = current.get("generation", 0)
    base_policy_id = current.get("policy_id", "")
    if not base_generation or not base_policy_id:
        fail(f"no committed policy for CAS apply: {body}")

    payload = {
        "source": read_yaml(yaml_name),
        "request_id": request_id,
        "operator_id": "control-plane-field-allowlist-e2e",
        "expected_base_generation": base_generation,
        "expected_base_policy_id": base_policy_id,
    }
    code, body = post_json(f"{CP_URL}/admin/control-plane/policy/apply", payload)
    if code not in (200, 201):
        if allow_no_changes and code == 400 and "RejectedNoChanges" in body:
            return
        fail(f"policy apply failed ({code}): {body}")
    result = json.loads(body)
    status = result.get("status", "")
    if status == "RejectedNoChanges":
        if allow_no_changes:
            return
        fail(f"apply rejected no changes for {yaml_name}: {body}")
    if not result.get("ok", True) and status not in ("Applied", "Accepted"):
        fail(f"apply not accepted: {body}")


def assert_cp_committed_reflects_reduction(
    baseline_generation: int, baseline_policy_id: str, *, timeout_s: float = 30.0
) -> None:
    deadline = time.time() + timeout_s
    last_body = ""
    while time.time() < deadline:
        code, body = get(f"{CP_URL}/admin/control-plane/policy/current")
        last_body = body
        if code != 200:
            time.sleep(0.5)
            continue
        current = json.loads(body)
        generation = current.get("generation", 0)
        policy_id = current.get("policy_id", "")
        if generation <= baseline_generation or policy_id == baseline_policy_id:
            time.sleep(0.5)
            continue

        resource_key = current.get("resource_key", "policy/default/runtime")
        version_url = (
            f"{CP_URL}/admin/control-plane/policy/version"
            f"?resource_key={resource_key}&generation={generation}"
        )
        vcode, vbody = get(version_url)
        if vcode != 200:
            time.sleep(0.5)
            continue
        version = json.loads(vbody)
        yaml_text = version.get("canonical_yaml", "")
        if "field_filter:" not in yaml_text or "allowlist" not in yaml_text:
            time.sleep(0.5)
            continue
        missing = [field for field in ALLOWED_AFTER_REDUCTION if field not in yaml_text]
        if missing:
            time.sleep(0.5)
            continue

        print(
            "PASS [allowlist-reduction]: committed policy advanced and version YAML reflects allowlist"
        )
        return
    fail(
        "committed policy did not reflect reduction apply within "
        f"{timeout_s}s; last body={last_body}"
    )


def assert_envoy_fields(
    present: list[str],
    absent: list[str],
    label: str,
    *,
    timeout_s: float = 30.0,
    cache_bust: bool = False,
) -> None:
    deadline = time.time() + timeout_s
    last_body = ""
    while time.time() < deadline:
        url = f"{ENVOY_URL}/api/v1/small"
        if cache_bust:
            url = f"{url}?__cp_field_e2e={int(time.time() * 1000)}"
        code, body = get(url)
        last_body = body
        if code != 200:
            time.sleep(0.5)
            continue
        data = json.loads(body)
        missing = [f for f in present if f not in data]
        leaked = [f for f in absent if f in data]
        if not missing and not leaked:
            print(f"PASS [{label}]: Envoy response shape OK")
            return
        time.sleep(0.5)
    fail(f"[{label}] Envoy field assertion failed within {timeout_s}s; last body={last_body}")


def main() -> None:
    if SKIP_OPEN_APPLY:
        print("==> Field-allowlist E2E: compose baseline (policy already applied)")
        poll_fleet_converged()
        assert_envoy_fields(
            present=ALLOWED_AFTER_REDUCTION + SENSITIVE_FIELDS,
            absent=[],
            label="compose-baseline",
        )
    else:
        print("==> Field-allowlist E2E: open policy (no include_fields)")
        apply_policy_yaml("field-allowlist-open.yaml", "cp-field-open", allow_no_changes=True)
        poll_fleet_converged()
        assert_envoy_fields(
            present=ALLOWED_AFTER_REDUCTION + SENSITIVE_FIELDS,
            absent=[],
            label="open-policy",
        )

    print("==> Field-allowlist E2E: reduction policy (committed policy on CP)")
    code, body = get(f"{CP_URL}/admin/control-plane/policy/current")
    if code != 200:
        fail(f"policy/current failed before reduction ({code}): {body}")
    baseline = json.loads(body)
    baseline_generation = baseline.get("generation", 0)
    baseline_policy_id = baseline.get("policy_id", "")
    apply_policy_yaml("field-allowlist-reduction.yaml", "cp-field-reduce")
    poll_fleet_converged(timeout_s=180.0)
    assert_cp_committed_reflects_reduction(baseline_generation, baseline_policy_id, timeout_s=60.0)
    assert_envoy_fields(
        present=ALLOWED_AFTER_REDUCTION,
        absent=SENSITIVE_FIELDS,
        label="allowlist-reduction",
        timeout_s=120.0,
        cache_bust=True,
    )

    print("PASS: control plane field-allowlist E2E")


if __name__ == "__main__":
    main()

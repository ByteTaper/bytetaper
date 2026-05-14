#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

import os
import sys
import json
import urllib.request
import urllib.error

ADMIN_URL = os.environ.get("BYTETAPER_ADMIN_URL", "http://bytetaper-extproc:18082")

def get(url):
    try:
        req = urllib.request.Request(url, method="GET")
        resp = urllib.request.urlopen(req, timeout=5.0)
        body = resp.read().decode('utf-8')
        return resp.getcode(), body
    except urllib.error.HTTPError as e:
        body = e.read().decode('utf-8')
        return e.code, body
    except Exception as e:
        return -1, str(e)

def post(url, data):
    try:
        json_bytes = json.dumps(data).encode('utf-8')
        req = urllib.request.Request(url, data=json_bytes, headers={"Content-Type": "application/json"}, method="POST")
        resp = urllib.request.urlopen(req, timeout=5.0)
        body = resp.read().decode('utf-8')
        return resp.getcode(), body
    except urllib.error.HTTPError as e:
        body = e.read().decode('utf-8')
        return e.code, body
    except Exception as e:
        return -1, str(e)

def fail(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)

print(f"Starting TaperQuery Admin HTTP E2E verification against {ADMIN_URL} ...")

print("\nStep 1: GET /admin/taperquery/policy/current")
code, body = get(f"{ADMIN_URL}/admin/taperquery/policy/current")
if code != 200:
    fail(f"Expected 200 for current policy, got {code}: {body}")

try:
    current_data = json.loads(body)
except Exception as e:
    fail(f"Could not parse current policy JSON: {e}")

if not current_data.get("ok"):
    fail("Expected ok=true in current policy response")

policy_identity = current_data.get("policy_identity")
if not policy_identity:
    fail("Expected non-empty policy_identity in current policy response")

print(f"Captured active policy_identity: {policy_identity}")

print("\nStep 2: POST /admin/taperquery/apply (mode=dry_run)")
dry_run_payload = {
    "source_type": "taperquery",
    "source": "policy \"my-policy\" { route \"dry_r\" when path prefix \"/dry\" {} }",
    "expected_base_identity": policy_identity,
    "mode": "dry_run"
}
code, body = post(f"{ADMIN_URL}/admin/taperquery/apply", dry_run_payload)
if code != 200:
    fail(f"Expected 200 for dry_run apply, got {code}: {body}")

try:
    dry_run_data = json.loads(body)
except Exception as e:
    fail(f"Could not parse dry_run response JSON: {e}")

if dry_run_data.get("status") != "DryRunReady":
    fail(f"Expected status=DryRunReady, got {dry_run_data.get('status')}")

print("Dry-run successfully validated candidate policy.")

print("\nStep 3: GET /admin/taperquery/policy/current (verify identity unchanged)")
code, body = get(f"{ADMIN_URL}/admin/taperquery/policy/current")
if code != 200:
    fail(f"Expected 200 for current policy check, got {code}: {body}")

check_data = json.loads(body)
if check_data.get("policy_identity") != policy_identity:
    fail(f"Expected policy_identity unchanged ({policy_identity}), got {check_data.get('policy_identity')}")

print("Active policy identity remained perfectly stable during dry-run.")

print("\nStep 4: POST /admin/taperquery/apply (stale CAS mismatch)")
stale_payload = {
    "source_type": "taperquery",
    "source": "policy \"my-policy\" { route \"stale_r\" when path prefix \"/stale\" {} }",
    "expected_base_identity": "stale-sha-12345",
    "mode": "apply"
}
code, body = post(f"{ADMIN_URL}/admin/taperquery/apply", stale_payload)
if code != 409:
    fail(f"Expected 409 Conflict for stale CAS mismatch, got {code}: {body}")

try:
    stale_data = json.loads(body)
except Exception as e:
    fail(f"Could not parse stale apply response JSON: {e}")

if stale_data.get("error_code") != "CAS_MISMATCH":
    fail(f"Expected error_code=CAS_MISMATCH, got {stale_data.get('error_code')}")

print("Stale candidate was correctly rejected with CAS_MISMATCH.")

print("\nPASS: All TaperQuery Admin HTTP E2E integration checks completed successfully!")

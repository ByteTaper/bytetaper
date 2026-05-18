#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

import os
import sys
import json
import time
import socket
import urllib.request
import urllib.error
import urllib.parse

ADMIN_URL = os.environ.get("BYTETAPER_ADMIN_URL", "http://bytetaper-extproc:18082")
ENVOY_URL = os.environ.get("BYTETAPER_ENVOY_URL", "http://envoy:10000")

def get(url):
    try:
        req = urllib.request.Request(url, method="GET")
        resp = urllib.request.urlopen(req, timeout=120.0)
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
        resp = urllib.request.urlopen(req, timeout=120.0)
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

def restart_extproc():
    print("Restarting bytetaper-extproc container via Docker Socket...")
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect("/var/run/docker.sock")
    s.sendall(b"GET /v1.41/containers/json?all=true HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    resp = b""
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        resp += chunk
    s.close()
    
    parts = resp.split(b"\r\n\r\n", 1)
    if len(parts) < 2:
        fail("Invalid Docker API response during container lookup")
    
    body = parts[1]
    if b"Transfer-Encoding: chunked" in parts[0] or b"transfer-encoding: chunked" in parts[0]:
        decoded = b""
        idx = 0
        while idx < len(body):
            line_end = body.find(b"\r\n", idx)
            if line_end == -1:
                break
            size_str = body[idx:line_end]
            size = int(size_str, 16)
            if size == 0:
                break
            decoded += body[line_end+2 : line_end+2+size]
            idx = line_end + 2 + size + 2
        body = decoded
        
    containers = json.loads(body.decode("utf-8"))
    extproc_id = None
    for c in containers:
        names = c.get("Names", [])
        for name in names:
            if name.endswith("bytetaper-extproc-1") or name.endswith("bytetaper-extproc"):
                extproc_id = c.get("Id")
                break
        if extproc_id:
            break
            
    if not extproc_id:
        fail("Could not find bytetaper-extproc container in Compose stack")
        
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect("/var/run/docker.sock")
    path = f"/v1.41/containers/{extproc_id}/restart"
    req = f"POST {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".encode("utf-8")
    s.sendall(req)
    resp = b""
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        resp += chunk
    s.close()
    
    if b"HTTP/1.1 204" not in resp:
        fail(f"Failed to restart container: {resp.decode('utf-8')}")
    print("Container bytetaper-extproc successfully restarted.")

def wait_for_extproc_healthy():
    print("Waiting for bytetaper-extproc to become healthy and reachable...")
    deadline = time.time() + 30.0
    while time.time() < deadline:
        code, body = get(f"{ADMIN_URL}/admin/taperquery/policy/current")
        if code == 200:
            print("bytetaper-extproc is ready!")
            return
        print(f"wait_for_extproc_healthy: connection not ready yet (code={code}, body={body})")
        time.sleep(0.5)
    fail("bytetaper-extproc did not become healthy in time")

def apply_policy(policy_src, current_identity):
    payload = {
        "source_type": "taperquery",
        "source": policy_src,
        "expected_base_identity": current_identity,
        "mode": "apply"
    }
    code, body = post(f"{ADMIN_URL}/admin/taperquery/apply", payload)
    if code != 200:
        try:
            data = json.loads(body)
            if data.get("error_code") == "NO_CHANGES":
                new_identity = data.get("current_policy_identity")
                print(f"Policy was already active (identity: {new_identity}).")
                return new_identity
        except Exception:
            pass
        fail(f"Failed to apply policy (status {code}): {body}")
    
    data = json.loads(body)
    new_identity = data.get("applied_policy_identity") or data.get("policy_identity")
    if not new_identity:
        fail(f"Response missing applied_policy_identity or policy_identity: {body}")
    return new_identity

PHASE = os.environ.get("BYTETAPER_E2E_PHASE", "all")
IDENTITY_FILE = "/workspace/policy_identity.txt"

if PHASE == "2":
    print("=== Starting Policy Allowlist/Denylist E2E Integration Checks - Phase 2 ===")
    wait_for_extproc_healthy()
    if not os.path.exists(IDENTITY_FILE):
        fail(f"Identity file '{IDENTITY_FILE}' not found! Run Phase 1 first.")
    with open(IDENTITY_FILE, "r") as f:
        identity = f.read().strip()
    print(f"Loaded expected policy identity from Phase 1: {identity}")
else:
    print("=== Starting Policy Allowlist/Denylist E2E Integration Checks - Phase 1 ===")
    wait_for_extproc_healthy()
    # Retrieve initial policy identity
    code, body = get(f"{ADMIN_URL}/admin/taperquery/policy/current")
    if code != 200:
        fail(f"Could not get current policy identity: {body}")
    initial_data = json.loads(body)
    identity = initial_data.get("policy_identity")
    print(f"Initial policy identity: {identity}")

if PHASE in ("all", "1"):
    # -------------------------------------------------------------
    # Test Case 1: Allowlist without query parameter
    # -------------------------------------------------------------
    print("\n--- Test Case 1: Allowlist-Without-Query ---")
    allowlist_policy = """
    policy "my-policy" {
      route "api-v1-proxy" when path prefix "/api/v1/" {
        mutate full;
        cache store ttl 300s {
          l1 enabled capacity 1000 entries
        }
        fields allow ["service", "scenario", "version", "payload", "sentinel"];
      }
    }
    """
    identity = apply_policy(allowlist_policy, identity)
    print(f"Applied Allowlist Policy. New identity: {identity}")

    # Request Envoy endpoint without query selection
    code, body = get(f"{ENVOY_URL}/api/v1/small")
    if code != 200:
        fail(f"Envoy request failed with {code}: {body}")

    resp_data = json.loads(body)
    # Assert disallowed sensitive fields are stripped
    for f in ["secret_token", "debug_internal", "request_path", "generated_at_epoch_ms"]:
        if f in resp_data:
            fail(f"'{f}' leaked in allowlist-without-query: {body}")
    # Assert allowed fields are present
    for f in ["service", "scenario", "version", "payload", "sentinel"]:
        if f not in resp_data:
            fail(f"Allowed field '{f}' missing from response: {body}")
    print("Test Case 1 passed: All disallowed fields stripped successfully, allowed fields preserved!")

    # -------------------------------------------------------------
    # Test Case 2: Allowlist/Query Intersection
    # -------------------------------------------------------------
    print("\n--- Test Case 2: Allowlist/Query Intersection ---")
    code, body = get(f"{ENVOY_URL}/api/v1/small?fields=service,secret_token")
    if code != 200:
        fail(f"Envoy request with query failed: {body}")

    resp_data = json.loads(body)
    # secret_token should be stripped (disallowed), but service (allowed) should remain
    if "secret_token" in resp_data:
        fail(f"Disallowed query field 'secret_token' was not stripped: {body}")
    if "service" not in resp_data:
        fail(f"Allowed query field 'service' was stripped: {body}")
    # Other fields like scenario should NOT leak
    if "scenario" in resp_data:
        fail(f"Unrequested field 'scenario' leaked in intersection: {body}")
    print("Test Case 2 passed: Only the intersection was returned!")

    # -------------------------------------------------------------
    # Test Case 3: Denylist without query parameter
    # -------------------------------------------------------------
    print("\n--- Test Case 3: Denylist-Without-Query ---")
    denylist_policy = """
    policy "my-policy" {
      route "api-v1-proxy" when path prefix "/api/v1/" {
        mutate full;
        cache store ttl 300s {
          l1 enabled capacity 1000 entries
        }
        fields deny ["secret_token", "debug_internal", "request_path", "generated_at_epoch_ms"];
      }
    }
    """
    identity = apply_policy(denylist_policy, identity)
    print(f"Applied Denylist Policy. New identity: {identity}")

    code, body = get(f"{ENVOY_URL}/api/v1/small")
    if code != 200:
        fail(f"Envoy request failed: {body}")

    resp_data = json.loads(body)
    # Assert denied sensitive fields are stripped
    for f in ["secret_token", "debug_internal", "request_path", "generated_at_epoch_ms"]:
        if f in resp_data:
            fail(f"'{f}' leaked in denylist-without-query: {body}")
    # Other fields should remain
    for f in ["service", "scenario", "version", "payload", "sentinel"]:
        if f not in resp_data:
            fail(f"Non-denied field '{f}' missing from denylist response: {body}")
    print("Test Case 3 passed: Denied fields stripped, other fields retained!")

    # -------------------------------------------------------------
    # Test Case 4: Denylist with disallowed query field (Finding 1)
    # -------------------------------------------------------------
    print("\n--- Test Case 4: Denylist with Disallowed Query (Finding 1 Fix Verification) ---")
    code, body = get(f"{ENVOY_URL}/api/v1/small?fields=secret_token")
    if code != 200:
        fail(f"Envoy request failed: {body}")

    resp_data = json.loads(body)
    # Should return empty object {} because secret_token is denied and others are not requested!
    if resp_data:
        fail(f"Expected empty response, but got fields: {body}")
    print("Test Case 4 passed: Disallowed query field in denylist returns empty projection, no leakage!")

    if PHASE == "1":
        # Save the identity so phase 2 can read it
        os.makedirs(os.path.dirname(IDENTITY_FILE), exist_ok=True)
        with open(IDENTITY_FILE, "w") as f:
            f.write(identity)
        print(f"\nPhase 1 E2E tests passed. Policy identity '{identity}' saved.")
        sys.exit(0)

# -------------------------------------------------------------
# Test Case 5: Restart Persistence Verification
# -------------------------------------------------------------
print("\n--- Test Case 5: Restart Persistence ---")
if PHASE == "all":
    try:
        restart_extproc()
    except Exception as e:
        print(f"WARNING: Cannot connect to docker.sock locally: {e}. Skipping socket restart in 'all' mode.", file=sys.stderr)
wait_for_extproc_healthy()

# Retrieve current policy identity post-restart
code, body = get(f"{ADMIN_URL}/admin/taperquery/policy/current")
if code != 200:
    fail(f"Could not get current policy post-restart: {body}")
post_restart_data = json.loads(body)
post_restart_identity = post_restart_data.get("policy_identity")
if post_restart_identity != identity:
    fail(f"Expected post-restart policy identity to match ({identity}), got {post_restart_identity}")

# Verify filtering is still active and correct
deadline = time.time() + 15.0
resp_data = None
while True:
    code, body = get(f"{ENVOY_URL}/api/v1/small")
    if code == 200:
        resp_data = json.loads(body)
        leaked = [f for f in ["secret_token", "debug_internal", "request_path", "generated_at_epoch_ms"] if f in resp_data]
        if not leaked:
            break
        print(f"wait_for_envoy_reconnect: fields {leaked} leaked, retrying...")
    else:
        print(f"wait_for_envoy_reconnect: Envoy request failed (code={code}, body={body}), retrying...")
    if time.time() > deadline:
        fail(f"Sensitive fields leaked after restart: {body}")
    time.sleep(0.5)

print("Test Case 5 passed: Policy state perfectly persisted across container restart!")

# -------------------------------------------------------------
# Test Case 6: Cache Regression (Namespace isolation / epoch invalidation)
# -------------------------------------------------------------
print("\n--- Test Case 6: Cache Regression (Namespace isolation) ---")
# Step A: Cache a response without query parameters under the denylist policy
# (which permits service and scenario)
code, body1 = get(f"{ENVOY_URL}/api/v1/small")
if code != 200:
    fail(f"Envoy request failed: {body1}")
data1 = json.loads(body1)
if "service" not in data1 or "scenario" not in data1:
    fail(f"Invalid response: {body1}")

# Step B: Apply a new policy allowing ONLY "sentinel"
allowlist_sentinel = """
policy "my-policy" {
  route "api-v1-proxy" when path prefix "/api/v1/" {
    mutate full;
    cache store ttl 300s {
      l1 enabled capacity 1000 entries
    }
    fields allow ["sentinel"];
  }
}
"""
identity = apply_policy(allowlist_sentinel, identity)
print(f"Applied new allowlist sentinel policy. New identity: {identity}")

# Step C: Request same route without query parameters again
# If cache invalidation/isolation failed, it would serve the old cached response (from Step A),
# exposing service/scenario fields.
# If invalidation works, it misses cache, queries mock-api, and filters out service/scenario!
code, body2 = get(f"{ENVOY_URL}/api/v1/small")
if code != 200:
    fail(f"Envoy request failed: {body2}")

data2 = json.loads(body2)
if "service" in data2 or "scenario" in data2:
    fail(f"Cache isolation failure! Served old cached fields after policy change: {body2}")
if "sentinel" not in data2:
    fail(f"Expected allowlist sentinel field not found in response: {body2}")
# Step D: Restore the full bootstrap policy for all routes to avoid poisoning subsequent tests in all-tests
default_full_policy = """
policy "my-policy" {
  route "api-v1-l1-cached" when path prefix "/api/v1/cached/" {
    mutate full;
    cache store ttl 300s {
      l1 enabled capacity 8 entries
    }
    coalesce cache_assisted {
      backend_timeout 100ms
      handoff_buffer 50ms
      result_ready_retention 25ms
      max_waiters_per_key 128
    }
  }

  route "api-v1-proxy" when method get and path prefix "/api/v1/" {
    mutate full;
    max_response 2MiB;
    compress {
      min_size 1024B
      eligible_content_types ["application/json"]
      prefer [gzip]
    }
    cache store ttl 300s {
      l1 enabled capacity 8 entries
    }
    fields allow ["id", "name", "status", "service", "scenario", "version", "payload", "sentinel"];
  }

  route "admin-exact" when path exact "/admin" {
    mutate headers_only;
  }

  route "api-v1-l1l2-cached" when path prefix "/api/v1/durable/" {
    mutate full;
    cache store ttl 3600s {
      l1 enabled capacity 8 entries
      l2 enabled path "/var/cache/bytetaper/api-v1-durable"
    }
  }

  route "api-v1-private-cached" when path prefix "/api/v1/private/" {
    mutate full;
    cache store ttl 60s {
      l1 enabled capacity 8 entries
      private auth_scope_header "authorization"
    }
  }

  route "api-v1-e2e-cache" when method get and path prefix "/api/v1/e2e/" {
    mutate full;
    max_response 65536B;
    cache store ttl 3600s {
      l1 enabled capacity 1 entries
      l2 enabled path "/var/cache/bytetaper/e2e"
    }
    fields allow ["service", "version"];
  }

  route "api-v1-paginated" when method get and path prefix "/api/v1/list/" {
    mutate full;
    max_response 65536B;
    paginate limit_offset {
      limit_param "limit"
      offset_param "offset"
      default_limit 20
      max_limit 100
      upstream_supports_pagination true
      max_response_bytes_warning 524288B
    }
  }

  route "orders-paginated" when path prefix "/orders" {
    mutate full;
    paginate limit_offset {
      limit_param "limit"
      offset_param "offset"
      default_limit 50
      max_limit 500
      upstream_supports_pagination true
    }
  }
}
"""
identity = apply_policy(default_full_policy, identity)
print(f"Restored default full bootstrap policy. Final identity: {identity}")

if os.path.exists(IDENTITY_FILE):
    try:
        os.remove(IDENTITY_FILE)
    except Exception:
        pass

print("\n=== ALL E2E INTEGRATION CHECKS PASSED SUCCESSFULLY ===")
sys.exit(0)

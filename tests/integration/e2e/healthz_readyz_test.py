#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

import os
import sys
import time
import signal
import subprocess
import urllib.request
import urllib.error

METRICS_URL = os.environ.get("BYTETAPER_METRICS_URL", "http://bytetaper-extproc:18081")

def get(url):
    try:
        resp = urllib.request.urlopen(url)
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

print("Starting healthz/readyz E2E verification...")

# --- Group 1: Verify the primary container instance ---
print("\n--- Group 1: Verifying active bytetaper-extproc service ---")

print("Test 1.1: Requesting /healthz (expect 200)...")
code, body = get(f"{METRICS_URL}/healthz")
if code != 200:
    fail(f"Expected 200 for /healthz, got {code}")
if body != "ok\n":
    fail(f"Expected 'ok\\n' for /healthz, got {repr(body)}")

print("Test 1.2: Requesting /readyz (expect 200)...")
code, body = get(f"{METRICS_URL}/readyz")
if code != 200:
    fail(f"Expected 200 for /readyz, got {code}")
if body != "ok\n":
    fail(f"Expected 'ok\\n' for /readyz, got {repr(body)}")

print("Test 1.3: Requesting unknown path (expect 404)...")
code, body = get(f"{METRICS_URL}/unknown")
if code != 404:
    fail(f"Expected 404 for /unknown, got {code}")

print("Test 1.4: Requesting /metrics (expect 200)...")
code, body = get(f"{METRICS_URL}/metrics")
if code != 200:
    fail(f"Expected 200 for /metrics, got {code}")
if "# HELP " not in body:
    fail("Expected prometheus metrics format in /metrics body")


# --- Group 2: Subprocess-based lifecyle and readiness contract testing ---
print("\n--- Group 2: Verifying lifecycle and non-200 readyz state transitions ---")

binary_path = "./build/bytetaper-extproc-server"
if not os.path.exists(binary_path):
    # Try absolute fallback
    binary_path = "/workspace/build/bytetaper-extproc-server"

cmd = [
    binary_path,
    "--listen-address", "127.0.0.1:28080",
    "--metrics-address", "127.0.0.1",
    "--metrics-port", "28081"
]

print(f"Launching test subprocess: {' '.join(cmd)}")
proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

sub_metrics_url = "http://127.0.0.1:28081"

# We must poll immediately to catch the "not started" phase
print("Test 2.1: Verifying early /readyz state (expect 503 'not started')...")
found_not_started = False
for _ in range(50):
    code, body = get(f"{sub_metrics_url}/readyz")
    if code == 503 and "not started" in body:
        found_not_started = True
        print(f"Successfully captured 'not started' state: {code} - {repr(body)}")
        break
    time.sleep(0.01)

if not found_not_started:
    proc.terminate()
    fail("Could not capture initial 'not started' /readyz 503 status")

# Wait a brief moment to allow full gRPC startup
print("Waiting for subprocess gRPC startup to complete...")
time.sleep(1.0)

print("Test 2.2: Verifying active /readyz state (expect 200 'ok')...")
code, body = get(f"{sub_metrics_url}/readyz")
if code != 200:
    proc.terminate()
    fail(f"Expected 200 'ok' after startup, got {code} - {repr(body)}")
print("Subprocess is active and healthy.")

# Now send SIGTERM to verify the "shutting down" transition contract
print("Test 2.3: Sending SIGTERM and capturing 'shutting down' transition...")
proc.send_signal(signal.SIGTERM)

found_shutting_down = False
for _ in range(100):
    code, body = get(f"{sub_metrics_url}/readyz")
    if code == 503 and "shutting down" in body:
        found_shutting_down = True
        print(f"Successfully captured 'shutting down' transition state: {code} - {repr(body)}")
        break
    time.sleep(0.005)

if not found_shutting_down:
    fail("Could not capture 'shutting down' transition state on SIGTERM")

# Wait for process to fully exit
stdout, stderr = proc.communicate(timeout=5)
exit_code = proc.returncode

print(f"Subprocess terminated with exit code: {exit_code}")
if exit_code != 0:
    print(f"Subprocess stderr:\n{stderr.decode('utf-8')}")
    fail(f"Subprocess exited with non-zero exit code: {exit_code}")

print("\nPASS: All healthz/readyz and lifecycle contract verifications completed successfully!")

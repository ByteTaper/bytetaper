#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

CONTAINER="$1"
if [ -z "$CONTAINER" ]; then
    echo "Usage: $0 <container-id>"
    exit 1
fi

echo "--- Running assert_runtime_health.sh ---"

# Case 1, 2, 3: healthz and readyz 200 OK
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:18081/healthz)
if [ "$HTTP_CODE" != "200" ]; then
    echo "[FAIL] /healthz returned HTTP $HTTP_CODE"
    exit 1
fi
echo "[PASS] /healthz returned 200 OK"

HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:18081/readyz)
if [ "$HTTP_CODE" != "200" ]; then
    echo "[FAIL] /readyz returned HTTP $HTTP_CODE"
    exit 1
fi
echo "[PASS] /readyz returned 200 OK"

# Case 7: id -u == 1001
UID_OUT=$(docker exec "$CONTAINER" id -u)
if [ "$UID_OUT" != "1001" ]; then
    echo "[FAIL] Container running as UID $UID_OUT instead of 1001"
    exit 1
fi
echo "[PASS] Container user ID is 1001"

# Case 11: Read-only root filesystem verification
# Attempt to write to a root directory path that is not tmpfs or volume
if docker exec "$CONTAINER" sh -c "echo 'test' > /root-test-write" 2>/dev/null; then
    echo "[FAIL] Root filesystem is writable"
    exit 1
fi
echo "[PASS] Root filesystem is read-only"

echo "SUCCESS: assert_runtime_health.sh assertions passed."

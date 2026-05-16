#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

CONTAINER="$1"
COMPOSE_FILE="${2:-docker-compose.prod.yml}"

if [ -z "$CONTAINER" ]; then
    echo "Usage: $0 <container-id> <compose-file>"
    exit 1
fi

if docker compose version >/dev/null 2>&1; then
    COMPOSE=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE=(docker-compose)
else
    echo "ERROR: neither 'docker compose' nor 'docker-compose' is available"
    exit 127
fi

echo "--- Running assert_runtime_l2_cache.sh ---"
echo "Using Compose command: ${COMPOSE[*]}"

# Check writable
if ! docker exec "$CONTAINER" sh -c "test -w /var/lib/bytetaper/l2-cache"; then
    echo "[FAIL] /var/lib/bytetaper/l2-cache is not writable"
    exit 1
fi
echo "[PASS] L2 cache directory is writable"

# Write a test file
docker exec "$CONTAINER" sh -c "echo 'persist-test' > /var/lib/bytetaper/l2-cache/persistence.test"

echo "Restarting service to test persistence..."
"${COMPOSE[@]}" -f "$COMPOSE_FILE" restart bytetaper

# Wait for ready again
sleep 5
CONTAINER_NEW=$("${COMPOSE[@]}" -f "$COMPOSE_FILE" ps -q bytetaper)

if ! docker exec "$CONTAINER_NEW" sh -c "grep -q 'persist-test' /var/lib/bytetaper/l2-cache/persistence.test"; then
    echo "[FAIL] L2 cache volume did not persist across restarts"
    exit 1
fi
echo "[PASS] L2 cache volume correctly persisted data across restart"

# Clean up test file
docker exec "$CONTAINER_NEW" sh -c "rm -f /var/lib/bytetaper/l2-cache/persistence.test"

echo "SUCCESS: assert_runtime_l2_cache.sh assertions passed."
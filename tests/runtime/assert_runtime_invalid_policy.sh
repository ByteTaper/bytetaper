#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

IMAGE="${1:-bytetaper-runtime:latest}"
COMPOSE_FILE="${2:-docker-compose.prod.yml}"

echo "--- Running assert_runtime_invalid_policy.sh ---"

if docker compose version >/dev/null 2>&1; then
    COMPOSE=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
    COMPOSE=(docker-compose)
else
    echo "ERROR: neither 'docker compose' nor 'docker-compose' is available"
    exit 127
fi

echo "Using Compose command: ${COMPOSE[*]}"

# Create a temporary override compose file mounting an invalid policy
INVALID_COMPOSE="/tmp/docker-compose.invalid.yml"

# Create an invalid policy file
INVALID_POLICY="/tmp/invalid-policy.yaml"
echo "invalid_yaml: [}" > "$INVALID_POLICY"

cat <<EOF > "$INVALID_COMPOSE"
services:
  bytetaper:
    image: ${IMAGE}
    command:
      - "--listen-address"
      - "0.0.0.0:18080"
      - "--policy-file"
      - "/etc/bytetaper/policy.yaml"
      - "--l2-cache-path"
      - "/var/lib/bytetaper/l2-cache"
      - "--metrics-address"
      - "0.0.0.0"
      - "--metrics-port"
      - "18081"
    volumes:
      - ${INVALID_POLICY}:/etc/bytetaper/policy.yaml:ro
      - bytetaper-l2-cache-invalid:/var/lib/bytetaper/l2-cache
    ports:
      - "18090:18080"
      - "18091:18081"
    read_only: true
    tmpfs:
      - /tmp
      - /var/run/bytetaper

volumes:
  bytetaper-l2-cache-invalid:
EOF

echo "Starting container with invalid policy..."
"${COMPOSE[@]}" -f "$INVALID_COMPOSE" up -d

# Wait briefly for startup/crash
sleep 5

# Check container status
# Handle Compose json output format variations gracefully
STATE=$("${COMPOSE[@]}" -f "$INVALID_COMPOSE" ps --format json | jq -r '.[] | select(.Service=="bytetaper") | .State' 2>/dev/null || true)
if [ "$STATE" = "running" ]; then
    # If running, readyz must NOT return 200
    if curl -fsS http://localhost:18091/readyz >/dev/null 2>&1; then
        echo "[FAIL] Container running and readyz returned 200 despite invalid policy"
        "${COMPOSE[@]}" -f "$INVALID_COMPOSE" down -v || true
        exit 1
    fi
    echo "[PASS] readyz never returned 200 for invalid policy"
else
    echo "[PASS] Container correctly exited/crashed on invalid policy"
fi

# Check logs for error keywords
LOGS=$("${COMPOSE[@]}" -f "$INVALID_COMPOSE" logs bytetaper 2>&1)
if ! echo "$LOGS" | grep -i "error\|invalid\|fail\|yaml" >/dev/null; then
    echo "[FAIL] Logs did not contain expected error keywords for invalid policy"
    "${COMPOSE[@]}" -f "$INVALID_COMPOSE" down -v || true
    exit 1
fi
echo "[PASS] Logs correctly captured invalid policy error"

# Cleanup
"${COMPOSE[@]}" -f "$INVALID_COMPOSE" down -v || true
rm -f "$INVALID_COMPOSE" "$INVALID_POLICY"

echo "SUCCESS: assert_runtime_invalid_policy.sh assertions passed."
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

IMAGE="${1:-bytetaper-runtime:latest}"
export BYTETAPER_IMAGE="${IMAGE}"
COMPOSE_FILE="${COMPOSE_FILE:-docker-compose.prod.yml}"
OUT_DIR="${BYTETAPER_RUNTIME_SMOKE_OUT_DIR:-dist/runtime-smoke}"
mkdir -p "${OUT_DIR}"

echo "Starting smoke runtime matrix for image: $IMAGE"

cleanup() {
    echo "Collecting post-test artifacts..."
    docker-compose -f "${COMPOSE_FILE}" ps > "${OUT_DIR}/compose-ps.txt" 2>&1 || true
    docker-compose -f "${COMPOSE_FILE}" logs bytetaper > "${OUT_DIR}/bytetaper-logs.txt" 2>&1 || true
    # capture container id safely
    CID=$(docker-compose -f "${COMPOSE_FILE}" ps -q bytetaper 2>/dev/null || true)
    if [ -n "$CID" ]; then
        docker inspect "$CID" > "${OUT_DIR}/inspect-bytetaper.json" 2>/dev/null || true
    fi
    echo "Tearing down compose stack..."
    docker-compose -f "${COMPOSE_FILE}" down -v || true
}
trap cleanup EXIT

docker-compose -f "${COMPOSE_FILE}" up -d

echo "Waiting for container readiness..."
# Wait up to 30 seconds for readiness
TIMEOUT=30
ELAPSED=0
READY=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    if curl -fsS http://localhost:18081/readyz > /dev/null 2>&1; then
        READY=1
        break
    fi
    sleep 2
    ELAPSED=$((ELAPSED + 2))
done

if [ $READY -eq 0 ]; then
    echo "ERROR: Container failed to become ready within ${TIMEOUT}s."
    docker-compose -f "${COMPOSE_FILE}" logs bytetaper
    exit 1
fi
echo "Container ready."

# Collect base endpoint responses
curl -fsS http://localhost:18081/healthz > "${OUT_DIR}/healthz.txt" || true
curl -fsS http://localhost:18081/readyz > "${OUT_DIR}/readyz.txt" || true
curl -fsS http://localhost:18081/metrics > "${OUT_DIR}/metrics.txt" || true

CONTAINER_NAME=$(docker-compose -f "${COMPOSE_FILE}" ps -q bytetaper)

echo "Running assertion suite..."
tests/runtime/assert_runtime_health.sh "$CONTAINER_NAME"
tests/runtime/assert_runtime_metrics.sh "http://localhost:18081"
tests/runtime/assert_runtime_policy_load.sh "$CONTAINER_NAME"
tests/runtime/assert_runtime_l2_cache.sh "$CONTAINER_NAME" "$COMPOSE_FILE"
tests/runtime/assert_runtime_admin_disabled.sh "$CONTAINER_NAME"
tests/runtime/assert_runtime_invalid_policy.sh "$IMAGE" "$COMPOSE_FILE"

echo "SUCCESS: All 11 runtime smoke matrix cases passed perfectly."

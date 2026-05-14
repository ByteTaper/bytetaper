#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

BASE_URL="${1:-http://localhost:18081}"

echo "--- Running assert_runtime_metrics.sh ---"

# Check /metrics HTTP status
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "${BASE_URL}/metrics")
if [ "$HTTP_CODE" != "200" ]; then
    echo "[FAIL] /metrics returned HTTP $HTTP_CODE"
    exit 1
fi
echo "[PASS] /metrics returned 200 OK"

# Check for metric prefix
METRICS_BODY=$(curl -fsS "${BASE_URL}/metrics")
if ! echo "$METRICS_BODY" | grep -q "bytetaper_cache_l1_hit_total"; then
    echo "[FAIL] bytetaper_cache_l1_hit_total metric not found in /metrics output"
    exit 1
fi
echo "[PASS] Expected cache metric found"

echo "SUCCESS: assert_runtime_metrics.sh assertions passed."

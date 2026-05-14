#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

CONTAINER="$1"
if [ -z "$CONTAINER" ]; then
    echo "Usage: $0 <container-id>"
    exit 1
fi

echo "--- Running assert_runtime_policy_load.sh ---"

# Check policy file readable
if ! docker exec "$CONTAINER" sh -c "test -r /etc/bytetaper/policy.yaml"; then
    echo "[FAIL] /etc/bytetaper/policy.yaml is not readable"
    exit 1
fi
echo "[PASS] Policy file is readable"

# Check policy file not writable
if docker exec "$CONTAINER" sh -c "test -w /etc/bytetaper/policy.yaml" 2>/dev/null; then
    echo "[FAIL] /etc/bytetaper/policy.yaml is writable"
    exit 1
fi
echo "[PASS] Policy file is read-only"

# Check system paths not writable
PATHS=(
    "/usr/local/bin"
    "/opt/bytetaper"
    "/etc/bytetaper"
)

for path in "${PATHS[@]}"; do
    if docker exec "$CONTAINER" sh -c "test -w $path" 2>/dev/null; then
        echo "[FAIL] System path $path is writable by runtime user"
        exit 1
    fi
    echo "[PASS] System path $path is not writable"
done

echo "SUCCESS: assert_runtime_policy_load.sh assertions passed."

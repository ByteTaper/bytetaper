#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

CONTAINER="$1"
if [ -z "$CONTAINER" ]; then
    echo "Usage: $0 <container-id>"
    exit 1
fi

echo "--- Running assert_runtime_admin_disabled.sh ---"

# Check port not exposed
EXPOSED=$(docker inspect "$CONTAINER" --format '{{json .Config.ExposedPorts}}')
if echo "$EXPOSED" | grep -q "18082"; then
    echo "[FAIL] Port 18082 is exposed in container configuration"
    exit 1
fi
echo "[PASS] Admin port 18082 is not exposed"

# Check curl fails/refused
if curl -fsS http://localhost:18082/admin/taperquery/policy/current >/dev/null 2>&1; then
    echo "[FAIL] Admin taperquery endpoint is unexpectedly reachable"
    exit 1
fi
echo "[PASS] Admin endpoint is unreachable from host"

# Check cmd does not contain --admin-enable-taperquery
CMD_ARGS=$(docker inspect "$CONTAINER" --format '{{json .Config.Cmd}}')
if echo "$CMD_ARGS" | grep -q -- "--admin-enable-taperquery"; then
    echo "[FAIL] Default command unexpectedly enables admin taperquery"
    exit 1
fi
echo "[PASS] TaperQuery admin flag is absent from default command"

echo "SUCCESS: assert_runtime_admin_disabled.sh assertions passed."

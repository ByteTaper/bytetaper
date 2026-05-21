#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

# shellcheck source=scripts/test/_control_plane_compose_lib.sh
source "${ROOT}/scripts/test/_control_plane_compose_lib.sh"
bytetaper_resolve_compose_cmd

export LOCAL_UID="${LOCAL_UID:-$(id -u)}"
export LOCAL_GID="${LOCAL_GID:-$(id -g)}"

BUILD_DIR="${BYTETAPER_CP_UNIT_BUILD_DIR:-build/control-plane-unit}"
UNIT_JOBS="${BYTETAPER_CP_UNIT_TEST_JOBS:-}"

echo "==> Control Plane unit tests (ctest -L control_plane_unit, build dir: ${BUILD_DIR})"
"${BYTETAPER_COMPOSE_CMD[@]}" -f docker-compose.yml run --rm bytetaper-unit-test bash -lc "
  set -eu
  BUILD_DIR='${BUILD_DIR}'
  JOBS='${UNIT_JOBS}'
  if [[ -z \"\${JOBS}\" ]]; then
    JOBS=\$(nproc)
  fi
  cmake -S . -B \"\${BUILD_DIR}\" -G Ninja \\
    -DBYTETAPER_ENABLE_GTEST_TESTS=ON \\
    -DBYTETAPER_ENABLE_INTEGRATION_TESTS=OFF \\
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \\
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  cmake --build \"\${BUILD_DIR}\" --target bytetaper_control_plane_unit_tests
  ctest --test-dir \"\${BUILD_DIR}\" -L control_plane_unit --output-on-failure --parallel \"\${JOBS}\"
"
echo "PASS: control-plane unit tests"

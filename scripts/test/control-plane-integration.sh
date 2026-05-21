#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

if docker compose version >/dev/null 2>&1; then
  COMPOSE=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
  COMPOSE=(docker-compose)
else
  echo "docker compose or docker-compose is required" >&2
  exit 1
fi

export LOCAL_UID="${LOCAL_UID:-$(id -u)}"
export LOCAL_GID="${LOCAL_GID:-$(id -g)}"

echo "==> Control Plane integration tests (ctest -L control_plane_integration)"
"${COMPOSE[@]}" run --rm bytetaper-unit-test bash -lc '
  set -eu
  cmake -S . -B build -G Ninja \
    -DBYTETAPER_ENABLE_GTEST_TESTS=ON \
    -DBYTETAPER_ENABLE_INTEGRATION_TESTS=ON \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  cmake --build build --target bytetaper_control_plane_integration_tests
  ctest --test-dir build -L control_plane_integration --output-on-failure
'
echo "PASS: control-plane integration tests"

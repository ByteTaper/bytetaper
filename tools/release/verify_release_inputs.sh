#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

echo "==> Verifying release inputs..."

PUBLISH="${BYTETAPER_RELEASE_PUBLISH:-false}"

if [[ -z "${BYTETAPER_VERSION:-}" ]]; then
  echo "ERROR: BYTETAPER_VERSION environment variable is required."
  exit 1
fi

if [[ "${PUBLISH}" == "true" ]]; then
  if ! [[ "${BYTETAPER_VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: Publishing mode requires a valid SemVer tag format (vMAJOR.MINOR.PATCH), got '${BYTETAPER_VERSION}'."
    exit 1
  fi
fi

# Assert required files exist
for f in docker/production.Dockerfile charts/bytetaper/Chart.yaml THIRD_PARTY_NOTICES.md; do
  if [[ ! -f "${f}" ]]; then
    echo "ERROR: Required file '${f}' does not exist."
    exit 1
  fi
done

if [[ ! -d "LICENSES" ]]; then
  echo "ERROR: Required directory 'LICENSES' does not exist."
  exit 1
fi

if [[ "${PUBLISH}" == "true" || "${BYTETAPER_VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  # Parse chart version field from Chart.yaml
  CHART_VERSION=$(grep "^version:" charts/bytetaper/Chart.yaml | awk '{print $2}' | tr -d '"')
  EXPECTED_VERSION="${BYTETAPER_VERSION#v}"

  if [[ "${CHART_VERSION}" != "${EXPECTED_VERSION}" ]]; then
    echo "ERROR: Chart version '${CHART_VERSION}' does not match expected version '${EXPECTED_VERSION}'."
    exit 1
  fi

  # Parse chart appVersion field from Chart.yaml
  CHART_APP_VERSION=$(grep "^appVersion:" charts/bytetaper/Chart.yaml | awk '{print $2}' | tr -d '"')

  if [[ "${CHART_APP_VERSION}" != "${BYTETAPER_VERSION}" ]]; then
    echo "ERROR: Chart appVersion '${CHART_APP_VERSION}' does not match tag version '${BYTETAPER_VERSION}'."
    exit 1
  fi
else
  echo "--> Non-publishing branch dry run: skipping strict chart version equality checks."
fi

echo "PASS: Release inputs verified successfully."

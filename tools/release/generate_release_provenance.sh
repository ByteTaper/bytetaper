#!/bin/bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Required environment variables (expected from GitHub Actions or local environment)
BYTETAPER_VERSION="${BYTETAPER_VERSION:-v0.0.0-dev}"
BYTETAPER_GIT_SHA="${BYTETAPER_GIT_SHA:-$(git rev-parse HEAD)}"
BYTETAPER_BUILD_DATE="${BYTETAPER_BUILD_DATE:-$(date -u +%Y-%m-%dT%H:%M:%SZ)}"

GITHUB_REF="${GITHUB_REF:-refs/heads/main}"
GITHUB_WORKFLOW="${GITHUB_WORKFLOW:-local-manual-build}"
GITHUB_RUN_ID="${GITHUB_RUN_ID:-0}"
GITHUB_RUN_ATTEMPT="${GITHUB_RUN_ATTEMPT:-1}"

DIGEST_FILE="dist/release/bytetaper-runtime-image-digest.txt"

if [[ ! -f "${DIGEST_FILE}" ]]; then
  echo "ERROR: Missing digest file ${DIGEST_FILE}"
  exit 1
fi

IMAGE_NAME=$(grep "^image=" "${DIGEST_FILE}" | cut -d= -f2)
IMAGE_DIGEST=$(grep "^digest=" "${DIGEST_FILE}" | cut -d= -f2)
IMAGE_REF=$(grep "^reference=" "${DIGEST_FILE}" | cut -d= -f2)

PROVENANCE_FILE="dist/release/bytetaper-provenance.json"

cat <<EOF > "${PROVENANCE_FILE}"
{
  "project": "bytetaper",
  "component": "bytetaper-runtime",
  "version": "${BYTETAPER_VERSION}",
  "git_sha": "${BYTETAPER_GIT_SHA}",
  "git_ref": "${GITHUB_REF}",
  "image_repository": "${IMAGE_NAME}",
  "image_digest": "${IMAGE_DIGEST}",
  "image_reference": "${IMAGE_REF}",
  "workflow_name": "${GITHUB_WORKFLOW}",
  "run_id": "${GITHUB_RUN_ID}",
  "run_attempt": "${GITHUB_RUN_ATTEMPT}",
  "build_date": "${BYTETAPER_BUILD_DATE}",
  "builder_os_image": "ubuntu-24.04",
  "dockerfile": "docker/production.Dockerfile",
  "release_manifest": "bytetaper-release-manifest.json",
  "sbom_artifacts": [
    "bytetaper-sbom.cdx.json",
    "bytetaper-sbom.spdx.json"
  ],
  "vulnerability_report_artifacts": [
    "bytetaper-vulnerability-report.json",
    "bytetaper-vulnerability-report.sarif"
  ]
}
EOF

echo "Provenance generated: ${PROVENANCE_FILE}"

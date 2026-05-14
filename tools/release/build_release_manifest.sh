#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

echo "==> Building release manifest..."

mkdir -p dist/release

if [[ -z "${BYTETAPER_VERSION:-}" || -z "${BYTETAPER_GIT_SHA:-}" || -z "${BYTETAPER_BUILD_DATE:-}" ]]; then
  echo "ERROR: BYTETAPER_VERSION, BYTETAPER_GIT_SHA, and BYTETAPER_BUILD_DATE environment variables are required."
  exit 1
fi

DIGEST_FILE="dist/release/bytetaper-runtime-image-digest.txt"
if [[ ! -f "${DIGEST_FILE}" ]]; then
  echo "ERROR: Image digest file '${DIGEST_FILE}' does not exist."
  exit 1
fi

IMG_REPO=$(grep "^image=" "${DIGEST_FILE}" | cut -d= -f2-)
IMG_TAG=$(grep "^tag=" "${DIGEST_FILE}" | cut -d= -f2-)
IMG_DIGEST=$(grep "^digest=" "${DIGEST_FILE}" | cut -d= -f2-)
IMG_REF=$(grep "^reference=" "${DIGEST_FILE}" | cut -d= -f2-)

CHART_VER="${BYTETAPER_VERSION#v}"
CHART_PKG=$(basename $(ls dist/release/bytetaper-*.tgz))

cat <<EOF > dist/release/bytetaper-release-manifest.json
{
  "name": "bytetaper",
  "release": {
    "version": "${BYTETAPER_VERSION}",
    "git_sha": "${BYTETAPER_GIT_SHA}",
    "short_sha": "${BYTETAPER_GIT_SHA:0:12}",
    "build_date": "${BYTETAPER_BUILD_DATE}"
  },
  "runtime_image": {
    "repository": "${IMG_REPO}",
    "tag": "${IMG_TAG}",
    "digest": "${IMG_DIGEST}",
    "reference": "${IMG_REF}"
  },
  "helm_chart": {
    "name": "bytetaper",
    "version": "${CHART_VER}",
    "package": "${CHART_PKG}"
  },
  "supply_chain": {
    "manifest": "bytetaper-supply-chain-manifest.json",
    "sbom_cyclonedx": "bytetaper-sbom.cdx.json",
    "sbom_spdx": "bytetaper-sbom.spdx.json",
    "vulnerability_report": "bytetaper-vulnerability-report.json",
    "sarif": "bytetaper-vulnerability-report.sarif"
  },
  "licenses": {
    "third_party_notices": "THIRD_PARTY_NOTICES.md",
    "licenses_archive": "LICENSES.zip"
  },
  "checksums": "bytetaper-checksums.txt"
}
EOF

echo "PASS: Release manifest generated successfully at dist/release/bytetaper-release-manifest.json"

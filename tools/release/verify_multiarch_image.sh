#!/bin/bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

IMAGE_REF="${1:-}"

if [[ -z "${IMAGE_REF}" ]]; then
  DIGEST_FILE="dist/release/bytetaper-runtime-image-digest.txt"
  if [[ -f "${DIGEST_FILE}" ]]; then
    IMAGE_REF=$(grep "^reference=" "${DIGEST_FILE}" | cut -d= -f2 | xargs)
  fi
fi

if [[ -z "${IMAGE_REF}" ]]; then
  echo "Usage: $0 <image-reference-with-digest>"
  exit 1
fi

echo "Verifying multi-arch manifest list: ${IMAGE_REF}"

INSPECT=$(docker buildx imagetools inspect "${IMAGE_REF}")

echo "Checking for linux/amd64... "
if echo "${INSPECT}" | grep -q "Platform:[[:space:]]*linux/amd64"; then
  echo "PASS"
else
  echo "FAIL"
  echo "Inspection output:"
  echo "${INSPECT}"
  exit 1
fi

echo "Checking for linux/arm64... "
if echo "${INSPECT}" | grep -q "Platform:[[:space:]]*linux/arm64"; then
  echo "PASS"
else
  echo "FAIL"
  echo "Inspection output:"
  echo "${INSPECT}"
  exit 1
fi

# NOTE:
# Docker Hub may not preserve or expose index-level annotations through
# `docker buildx imagetools inspect` in the same way GHCR does. The runtime
# image still carries OCI labels, and those labels are validated earlier from
# the locally built image. This step should validate the remote multi-arch
# contract, not fail release publishing due to registry-specific annotation
# rendering differences.
if echo "${INSPECT}" | grep -q "unknown/unknown"; then
  echo "INFO: Manifest contains unknown/unknown attestation manifests. This is expected when BuildKit attaches provenance/SBOM attestation manifests."
fi

DIGEST=$(printf "%s\n" "${INSPECT}" | awk '/^Digest:[[:space:]]*sha256:/ {print $2; exit}')
if [[ ! "${DIGEST}" =~ ^sha256:[0-9a-f]{64}$ ]]; then
  echo "ERROR: Could not resolve a valid manifest-list digest from inspection."
  exit 1
fi

mkdir -p dist/release
cat <<EOF > dist/release/bytetaper-runtime-platforms.txt
linux/amd64
linux/arm64
EOF

echo "Remote manifest digest: ${DIGEST}"
echo "Verification complete. Platforms recorded in dist/release/bytetaper-runtime-platforms.txt"

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

RUNTIME_OS_IMAGE="${BYTETAPER_RUNTIME_OS_IMAGE:-ubuntu:26.04}"

ANNOTATION_WARNING_FILE="dist/release/bytetaper-runtime-manifest-annotation-warning.txt"
mkdir -p dist/release

echo "Checking manifest-list OS image annotations... "
if echo "${INSPECT}" | grep -F -q "org.opencontainers.image.base.name: ${RUNTIME_OS_IMAGE}" &&
   echo "${INSPECT}" | grep -F -q "io.bytetaper.os-image: ${RUNTIME_OS_IMAGE}"; then
  echo "PASS"
  rm -f "${ANNOTATION_WARNING_FILE}"
else
  echo "WARNING"
  cat <<EOF | tee "${ANNOTATION_WARNING_FILE}"
WARNING: Remote manifest-list OS image annotations were not visible in docker buildx imagetools inspect output.

Expected annotations:
  org.opencontainers.image.base.name: ${RUNTIME_OS_IMAGE}
  io.bytetaper.os-image: ${RUNTIME_OS_IMAGE}

This is treated as a warning because registries can differ in how they preserve or render index-level annotations.
The runtime image OCI labels are validated earlier from the locally built image.
This warning is especially relevant when publishing to Docker Hub, while GHCR may expose these annotations differently.
EOF
  echo "Inspection output:"
  echo "${INSPECT}"
fi

if echo "${INSPECT}" | grep -q "unknown/unknown"; then
  echo "INFO: Manifest contains unknown/unknown attestation manifests. This is expected when BuildKit attaches provenance/SBOM attestation manifests."
fi

DIGEST=$(printf "%s\n" "${INSPECT}" | awk '/^Digest:[[:space:]]*sha256:/ {print $2; exit}')
if [[ ! "${DIGEST}" =~ ^sha256:[0-9a-f]{64}$ ]]; then
  echo "ERROR: Could not resolve a valid manifest-list digest from inspection."
  exit 1
fi

cat <<EOF > dist/release/bytetaper-runtime-platforms.txt
linux/amd64
linux/arm64
EOF

echo "Remote manifest digest: ${DIGEST}"
echo "Verification complete. Platforms recorded in dist/release/bytetaper-runtime-platforms.txt"

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
REQUIRE_REMOTE_INDEX_ANNOTATIONS="${BYTETAPER_REQUIRE_REMOTE_INDEX_ANNOTATIONS:-false}"
ANNOTATION_WARNING_FILE="dist/release/bytetaper-runtime-manifest-annotation-warning.txt"
mkdir -p dist/release

case "${IMAGE_REF}" in
  docker.io/*|*/docker.io/*)
    REGISTRY_KIND="dockerhub"
    ;;
  ghcr.io/*|*/ghcr.io/*)
    REGISTRY_KIND="ghcr"
    ;;
  *)
    REGISTRY_KIND="generic"
    ;;
esac

echo "Detected registry kind: ${REGISTRY_KIND}"
echo "Checking manifest-list OS image annotations... "
if echo "${INSPECT}" | grep -F -q "org.opencontainers.image.base.name: ${RUNTIME_OS_IMAGE}" &&
   echo "${INSPECT}" | grep -F -q "io.bytetaper.os-image: ${RUNTIME_OS_IMAGE}"; then
  echo "PASS"
  rm -f "${ANNOTATION_WARNING_FILE}"
else
  cat <<EOF | tee "${ANNOTATION_WARNING_FILE}"
WARNING: Remote manifest-list OS image annotations were not visible in docker buildx imagetools inspect output.

Registry kind: ${REGISTRY_KIND}
Expected annotations:
  org.opencontainers.image.base.name: ${RUNTIME_OS_IMAGE}
  io.bytetaper.os-image: ${RUNTIME_OS_IMAGE}

This is warning-only by default because registries differ in how they preserve or render index-level annotations.
ByteTaper still performs strict local runtime image label validation before publishing.
Set BYTETAPER_REQUIRE_REMOTE_INDEX_ANNOTATIONS=true to make this check fail the release.
EOF
  echo "Inspection output:"
  echo "${INSPECT}"

  if [[ "${REQUIRE_REMOTE_INDEX_ANNOTATIONS}" == "true" ]]; then
    echo "ERROR: BYTETAPER_REQUIRE_REMOTE_INDEX_ANNOTATIONS=true and required remote annotations were not visible."
    exit 1
  fi
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
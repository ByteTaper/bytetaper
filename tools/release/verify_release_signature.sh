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
  echo "Example: $0 ghcr.io/haluan/bytetaper-runtime@sha256:..."
  exit 1
fi

echo "Verifying signature and attestations for: ${IMAGE_REF}"
echo "--------------------------------------------------------"

# We expect the official release workflow to sign the image
ISSUER="https://token.actions.githubusercontent.com"
IDENTITY_REGEXP="https://github.com/haluan/bytetaper/.github/workflows/release.yml@refs/tags/v.*"

echo -n "Checking signature... "
if cosign verify --certificate-oidc-issuer "${ISSUER}" --certificate-identity-regexp "${IDENTITY_REGEXP}" "${IMAGE_REF}" >/dev/null 2>&1; then
  echo "PASS"
else
  echo "FAIL"
  exit 1
fi

echo -n "Checking CycloneDX SBOM attestation... "
if cosign verify-attestation --type cyclonedx --certificate-oidc-issuer "${ISSUER}" --certificate-identity-regexp "${IDENTITY_REGEXP}" "${IMAGE_REF}" >/dev/null 2>&1; then
  echo "PASS"
else
  echo "FAIL"
  exit 1
fi

echo -n "Checking SPDX SBOM attestation... "
if cosign verify-attestation --type spdx --certificate-oidc-issuer "${ISSUER}" --certificate-identity-regexp "${IDENTITY_REGEXP}" "${IMAGE_REF}" >/dev/null 2>&1; then
  echo "PASS"
else
  echo "FAIL"
  exit 1
fi

echo -n "Checking SLSA Provenance attestation... "
if cosign verify-attestation --type slsaprovenance --certificate-oidc-issuer "${ISSUER}" --certificate-identity-regexp "${IDENTITY_REGEXP}" "${IMAGE_REF}" >/dev/null 2>&1; then
  echo "PASS"
else
  echo "FAIL"
  exit 1
fi

echo "--------------------------------------------------------"
echo "All critical verification checks passed."

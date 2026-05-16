#!/bin/bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

BYTETAPER_RELEASE_PUBLISH="${BYTETAPER_RELEASE_PUBLISH:-false}"
DIGEST_FILE="dist/release/bytetaper-runtime-image-digest.txt"
SUMMARY_FILE="dist/release/bytetaper-signing-summary.json"

if [[ ! -f "${DIGEST_FILE}" ]]; then
  echo "ERROR: Missing digest file ${DIGEST_FILE}"
  exit 1
fi

IMAGE_REF=$(grep "^reference=" "${DIGEST_FILE}" | cut -d= -f2 | xargs)

# Validate reference format
if ! [[ "${IMAGE_REF}" =~ @sha256:[0-9a-f]{64}$ ]]; then
  echo "ERROR: Image reference '${IMAGE_REF}' must include a sha256 digest."
  exit 1
fi

if [[ "${BYTETAPER_RELEASE_PUBLISH}" != "true" ]]; then
  echo "Release publish mode is disabled. Skipping image signing."
  cat <<EOF > "${SUMMARY_FILE}"
{
  "result": "skipped",
  "reason": "release publish mode is disabled"
}
EOF
  exit 0
fi

echo "Signing image: ${IMAGE_REF}"
cosign sign --yes "${IMAGE_REF}"

echo "Attaching CycloneDX SBOM..."
cosign attest --yes --predicate dist/release/bytetaper-sbom.cdx.json \
  --type cyclonedx "${IMAGE_REF}"

echo "Attaching SPDX SBOM..."
cosign attest --yes --predicate dist/release/bytetaper-sbom.spdx.json \
  --type spdx "${IMAGE_REF}"

echo "Attaching SLSA Provenance..."
cosign attest --yes --predicate dist/release/bytetaper-provenance.json \
  --type slsaprovenance "${IMAGE_REF}"

cat <<EOF > "${SUMMARY_FILE}"
{
  "result": "signed",
  "signing_mode": "keyless-oidc",
  "oidc_issuer": "https://token.actions.githubusercontent.com",
  "image_reference": "${IMAGE_REF}",
  "github_repository": "${GITHUB_REPOSITORY:-unknown}",
  "git_ref": "${GITHUB_REF:-unknown}",
  "workflow_name": "${GITHUB_WORKFLOW:-unknown}",
  "workflow_run_id": "${GITHUB_RUN_ID:-unknown}"
}
EOF

echo "Signing and attestation complete. Summary written to ${SUMMARY_FILE}"

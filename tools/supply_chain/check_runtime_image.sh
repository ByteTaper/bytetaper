#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
    echo "Usage: $0 <image-tag>"
    exit 1
fi

OUT_DIR="${BYTETAPER_SUPPLY_CHAIN_OUT_DIR:-dist/supply-chain}"
FAIL_SEV="${BYTETAPER_CVE_FAIL_SEVERITY:-CRITICAL}"
ALLOWLIST="${BYTETAPER_CVE_ALLOWLIST:-tools/supply_chain/cve-allowlist.yaml}"
VERSION="${BYTETAPER_VERSION:-dev}"
GIT_SHA="${BYTETAPER_GIT_SHA:-unknown}"
BUILD_DATE="${BYTETAPER_BUILD_DATE:-unknown}"

mkdir -p "$OUT_DIR"

echo "[1/5] Inspect image"
docker image inspect "$IMAGE" > "$OUT_DIR/bytetaper-runtime-image-inspect.json"

echo "[2/5] Verify license artifacts"
REQUIRED_LICENSES=(
    "/opt/bytetaper/THIRD_PARTY_NOTICES.md"
    "/opt/bytetaper/LICENSES/AGPL-3.0-only.txt"
    "/opt/bytetaper/LICENSES/Apache-2.0.txt"
    "/opt/bytetaper/LICENSES/BSD-3-Clause-Protocol-Buffers.txt"
    "/opt/bytetaper/LICENSES/LicenseRef-Commercial.txt"
    "/opt/bytetaper/LICENSES/MIT-quill.txt"
    "/opt/bytetaper/LICENSES/MIT-yaml-cpp.txt"
)

# Verify each file exists inside the image
for path in "${REQUIRED_LICENSES[@]}"; do
    docker run --rm --entrypoint /bin/sh "$IMAGE" -c "test -f $path" || {
        echo "ERROR: Required license artifact missing inside image: $path"
        exit 1
    }
done

printf "%s\n" "${REQUIRED_LICENSES[@]}" > "$OUT_DIR/bytetaper-runtime-license-report.txt"

echo "[3/5] Generate SBOM"
trivy image --format cyclonedx --output "$OUT_DIR/bytetaper-runtime-sbom.cdx.json" "$IMAGE"
trivy image --format spdx-json --output "$OUT_DIR/bytetaper-runtime-sbom.spdx.json" "$IMAGE"

echo "[4/5] Scan vulnerabilities"
python3 tools/supply_chain/validate_cve_allowlist.py "$ALLOWLIST" "$OUT_DIR/.trivyignore"

trivy image \
  --format json \
  --output "$OUT_DIR/bytetaper-runtime-vulnerability-report.json" \
  --severity "$FAIL_SEV" \
  --ignorefile "$OUT_DIR/.trivyignore" \
  "$IMAGE"

trivy image \
  --format sarif \
  --output "$OUT_DIR/bytetaper-runtime-vulnerability-report.sarif" \
  --severity "$FAIL_SEV" \
  --ignorefile "$OUT_DIR/.trivyignore" \
  "$IMAGE"

echo "[5/5] Write supply-chain manifest"
cat <<EOF > "$OUT_DIR/bytetaper-supply-chain-manifest.json"
{
  "name": "bytetaper",
  "component": "bytetaper-runtime",
  "image": "$IMAGE",
  "version": "$VERSION",
  "git_sha": "$GIT_SHA",
  "build_date": "$BUILD_DATE",
  "sbom": {
    "cyclonedx": "bytetaper-runtime-sbom.cdx.json",
    "spdx": "bytetaper-runtime-sbom.spdx.json"
  },
  "vulnerability_report": {
    "json": "bytetaper-runtime-vulnerability-report.json",
    "sarif": "bytetaper-runtime-vulnerability-report.sarif"
  },
  "license_artifacts": {
    "third_party_notices": "/opt/bytetaper/THIRD_PARTY_NOTICES.md",
    "licenses_dir": "/opt/bytetaper/LICENSES"
  },
  "policy": {
    "fail_severity": "$FAIL_SEV",
    "allowlist": "$ALLOWLIST"
  }
}
EOF

# Exit non-zero if vulnerabilities found
echo "Executing final vulnerability gating check..."
trivy image \
  --exit-code 1 \
  --severity "$FAIL_SEV" \
  --ignorefile "$OUT_DIR/.trivyignore" \
  "$IMAGE"

echo "SUCCESS: Supply chain readiness check completed successfully."
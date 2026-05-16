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
IGNORE_FILE="$OUT_DIR/.cveignore"
VULN_JSON="$OUT_DIR/bytetaper-runtime-vulnerability-report.json"

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

echo "[3/5] Generate SBOM with Syft"
syft "$IMAGE" -o cyclonedx-json="$OUT_DIR/bytetaper-runtime-sbom.cdx.json"
syft "$IMAGE" -o spdx-json="$OUT_DIR/bytetaper-runtime-sbom.spdx.json"
syft "$IMAGE" -o syft-json="$OUT_DIR/bytetaper-runtime-sbom.syft.json"

echo "[4/5] Scan vulnerabilities with Grype"
python3 tools/supply_chain/validate_cve_allowlist.py "$ALLOWLIST" "$IGNORE_FILE"

grype "$IMAGE" -o json > "$VULN_JSON"
grype "$IMAGE" -o sarif > "$OUT_DIR/bytetaper-runtime-vulnerability-report.sarif"

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
    "spdx": "bytetaper-runtime-sbom.spdx.json",
    "syft": "bytetaper-runtime-sbom.syft.json"
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
    "allowlist": "$ALLOWLIST",
    "ignore_file": "$IGNORE_FILE"
  },
  "tools": {
    "sbom": "syft",
    "vulnerability_scanner": "grype"
  }
}
EOF

# Exit non-zero if vulnerabilities at or above the gate severity are found outside the allowlist.
echo "Executing final vulnerability gating check..."
python3 - "$VULN_JSON" "$IGNORE_FILE" "$FAIL_SEV" <<'PY'
import json
import sys
from pathlib import Path

report_path = Path(sys.argv[1])
ignore_path = Path(sys.argv[2])
fail_severity = sys.argv[3].strip().lower()

severity_rank = {
    "unknown": 0,
    "negligible": 1,
    "low": 2,
    "medium": 3,
    "high": 4,
    "critical": 5,
}

if fail_severity not in severity_rank:
    print(f"ERROR: Unsupported BYTETAPER_CVE_FAIL_SEVERITY '{sys.argv[3]}'.")
    sys.exit(1)

allowed = set()
if ignore_path.exists():
    allowed = {
        line.strip()
        for line in ignore_path.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    }

report = json.loads(report_path.read_text())
violations = []

for match in report.get("matches", []):
    vuln = match.get("vulnerability", {})
    vuln_id = vuln.get("id", "")
    severity = str(vuln.get("severity", "unknown")).lower()
    if not vuln_id or vuln_id in allowed:
        continue
    if severity_rank.get(severity, 0) >= severity_rank[fail_severity]:
        artifact = match.get("artifact", {})
        package_name = artifact.get("name", "unknown-package")
        package_version = artifact.get("version", "unknown-version")
        violations.append((vuln_id, severity.upper(), package_name, package_version))

if violations:
    print(f"ERROR: Found {len(violations)} vulnerabilities at or above {fail_severity.upper()} severity outside the allowlist:")
    for vuln_id, severity, package_name, package_version in violations:
        print(f"- {vuln_id} [{severity}] {package_name}@{package_version}")
    sys.exit(1)

print(f"PASS: No vulnerabilities at or above {fail_severity.upper()} severity outside the allowlist.")
PY

echo "SUCCESS: Supply chain readiness check completed successfully."

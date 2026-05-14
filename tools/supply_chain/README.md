 # ByteTaper Supply Chain Readiness Tools

This directory contains automated scripts and configuration files designed to generate, validate, and audit the software supply chain artifacts of the ByteTaper production runtime container image.

## Overview

The ByteTaper supply chain readiness suite enforces rigorous verification gates before any container image is promoted to production:
1. **Container Provenance Inspection**: Captures OCI image labels, environmental parameters, and build metadata.
2. **License & Legal Attribution**: Validates that all open-source software license texts and attribution notices are properly embedded inside the container filesystem.
3. **Software Bill of Materials (SBOM)**: Generates highly accurate CycloneDX and SPDX manifests.
4. **Vulnerability Scanning**: Audits the runtime container against known CVE databases with configurable severity thresholds and audit allowlists.
5. **Supply Chain Manifest**: Synthesizes all verification results into a unified, traceable JSON summary.

---

## Prerequisites

To execute the supply chain checks locally, ensure the following utilities are installed:
- `docker` (Engine 20.10+ / Compose v2)
- `trivy` (v0.50.0+)
- `jq`

---

## Local Usage

Execute the verification harness against a local or remote container tag:

```bash
# Build the production candidate image
docker build -f docker/production.Dockerfile -t bytetaper-runtime:local .

# Execute supply chain checks
tools/supply_chain/check_runtime_image.sh bytetaper-runtime:local
```

### Supported Environment Variables

| Variable | Default | Purpose |
|---|---|---|
| `BYTETAPER_SUPPLY_CHAIN_OUT_DIR` | `dist/supply-chain` | Output directory for all generated reports and manifests |
| `BYTETAPER_CVE_FAIL_SEVERITY` | `CRITICAL` | Comma-separated minimum severity levels that trigger a CI scan failure |
| `BYTETAPER_CVE_ALLOWLIST` | `tools/supply_chain/cve-allowlist.yaml` | Path to the Trivy vulnerability allowlist configuration file |
| `BYTETAPER_VERSION` | `dev` | Semantic version string injected into the final manifest |
| `BYTETAPER_GIT_SHA` | `unknown` | Commit SHA revision injected into the final manifest |
| `BYTETAPER_BUILD_DATE` | `unknown` | Timestamp string injected into the final manifest |

---

## Generated Output Artifacts

Upon successful completion, the output directory (`dist/supply-chain/`) will contain:

```text
dist/supply-chain/
├── bytetaper-runtime-image-inspect.json          # Raw Docker inspect metadata
├── bytetaper-runtime-license-report.txt          # Discovered embedded license paths
├── bytetaper-runtime-sbom.cdx.json               # CycloneDX SBOM manifest
├── bytetaper-runtime-sbom.spdx.json              # SPDX-JSON SBOM manifest
├── bytetaper-runtime-vulnerability-report.json   # Full Trivy CVE scan JSON report
├── bytetaper-runtime-vulnerability-report.sarif  # SARIF formatted CVE report for GitHub Security
└── bytetaper-supply-chain-manifest.json          # Master synthesis manifest
```

---

## Vulnerability Allowlist Management

False positives or accepted risks must be explicitly documented in `tools/supply_chain/cve-allowlist.yaml` following the standard schema:

```yaml
allowlisted:
  - id: "CVE-YYYY-XXXX"
    package: "libexample"
    reason: "Static linking omission / Unreachable code path"
    expires: "2026-12-31"
    owner: "secops@example.com"
```

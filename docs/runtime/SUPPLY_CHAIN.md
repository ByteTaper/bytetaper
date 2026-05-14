# Production Supply Chain Architecture

ByteTaper incorporates an automated, zero-trust software supply chain audit harness into its CI pipeline to guarantee legal compliance, traceability, and vulnerability remediation before production deployment.

## Architecture & Workflow

Every production runtime container candidate undergoes a multi-tiered analysis driven by `tools/supply_chain/check_runtime_image.sh` and **Trivy**:

```text
┌────────────────────────────────────────────────────────┐
│               Production Candidate Image               │
└───────────────────────────┬────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│             Stage 1: Container Provenance              │
│       Captures OCI labels, versions, and git SHA       │
└───────────────────────────┬────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│             Stage 2: License Verification              │
│   Audits internal filesystem for 7 required licenses   │
└───────────────────────────┬────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│                 Stage 3: SBOM Creation                 │
│      Emits SPDX-JSON and CycloneDX specifications      │
└───────────────────────────┬────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│               Stage 4: Vulnerability Scan              │
│       Evaluates CVEs against YAML audit allowlist      │
└───────────────────────────┬────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│            Stage 5: Supply Chain Manifest              │
│    Synthesizes all audit references into master JSON   │
└────────────────────────────────────────────────────────┘
```

---

## Supply Chain Artifact Reference

The CI workflow publishes a comprehensive audit bundle attached to every build:

| Artifact Name | Specification Format | Purpose |
|---|---|---|
| `bytetaper-supply-chain-manifest.json` | Custom JSON | Master index referencing all generated reports, image SHAs, and active security policies |
| `bytetaper-runtime-sbom.cdx.json` | CycloneDX v1.5 | Full software bill of materials optimized for enterprise dependency tracking |
| `bytetaper-runtime-sbom.spdx.json` | SPDX 2.3 JSON | Standardized SBOM manifest for legal and open-source attribution audits |
| `bytetaper-runtime-vulnerability-report.json` | Trivy JSON | Complete, unfiltered CVE analysis report including base OS layers and C++ libraries |
| `bytetaper-runtime-vulnerability-report.sarif` | SARIF | Static Analysis Results Interchange Format for native GitHub Security tab rendering |
| `bytetaper-runtime-license-report.txt` | Plaintext | List of verified open-source software license agreements discovered inside the container |

---

## Security Gates & Allowlist Policy

The CI pipeline enforces a strict failure threshold for any unmitigated vulnerabilities meeting or exceeding the configured severity (`CRITICAL` by default).

### Remediating False Positives
If a reported vulnerability is determined to be a false positive (e.g., unreachable code path or unused development utility), it must be formally documented in `tools/supply_chain/cve-allowlist.yaml`:

```yaml
allowlisted:
  - id: "CVE-2026-99999"
    package: "libzstd"
    reason: "Vulnerability affects experimental compression modes not invoked by ByteTaper"
    expires: "2026-12-31"
    owner: "secops@bytetaper.io"
```

> [!IMPORTANT]  
> All allowlist entries must include an explicit expiration date (`expires`). When an entry expires, CI will automatically resume failing until the underlying package is upgraded or the risk exception is formally re-reviewed. All allowlist entries are dynamically validated by `validate_cve_allowlist.py` prior to scanning. Any entry missing required fields, containing malformed CVE IDs, or whose `expires` date has passed in UTC will trigger an immediate CI failure.

---

## Legal Notice Freshness Guard

To prevent supply chain attribution drift, `check_notice_freshness.sh` automatically evaluates git commits during CI. If dependency-sensitive files (`Dockerfile`, `CMakeLists.txt`, `cmake/*`, `proto/*`) are modified without a corresponding update to `THIRD_PARTY_NOTICES.md` or `LICENSES/`, a GitHub Actions warning is emitted.

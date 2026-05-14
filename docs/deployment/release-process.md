# ByteTaper Release & Publishing Manual

This manual details the automated release publishing pipeline (`BT-DIST-002`) for deploying the hardened ByteTaper API Performance Gateway runtime.

## 1. Prerequisites & Tag Format

Releases are fully automated via GitHub Actions and triggered exclusively by pushing SemVer tags matching the format `vMAJOR.MINOR.PATCH` (e.g., `v0.1.0`).

> [!IMPORTANT]
> Manual workflow dispatch (`publish=true`) requires running against a valid release tag. Triggering a publish run on a non-tag branch will fail immediately. Running on a branch with `publish=false` executes a safe, non-publishing dry run that synthesizes verification artifacts using synthetic `v0.1.0-dryrun-<shortsha>` version metadata without mutating registry or GitHub Release state.

### Creating a Release Tag
```bash
git tag -a v0.1.0 -m "Release v0.1.0"
git push origin v0.1.0
```

---

## 2. Published Artifacts

Every GitHub Release contains the complete, cryptographically verifiable supply-chain and deployment bundle:

| Artifact | Description |
|---|---|
| `bytetaper-release-manifest.json` | Master JSON record containing exact Git SHAs, digests, and artifact references. |
| `bytetaper-supply-chain-manifest.json` | Master supply-chain attestation tying image layers, SBOMs, and license records. |
| `bytetaper-0.1.0.tgz` | Official packaged Helm chart for deploying the gateway release. |
| `bytetaper-runtime-image-digest.txt` | Text record of the GHCR OCI image digest and repository reference. |
| `bytetaper-checksums.txt` | Master SHA256 checksums file covering every attached artifact. |
| `bytetaper-sbom.cdx.json` | CycloneDX format Software Bill of Materials (SBOM). |
| `bytetaper-sbom.spdx.json` | SPDX format Software Bill of Materials (SBOM). |
| `bytetaper-vulnerability-report.json` | Comprehensive Trivy container vulnerability scan report. |
| `bytetaper-vulnerability-report.sarif` | SARIF format vulnerability report for IDE and SIEM consumption. |
| `THIRD_PARTY_NOTICES.md` | Canonical dependency legal attributions and copyright notices. |
| `LICENSES.zip` | Full archive of raw dependency license files. |

---

## 3. Operator Consumption

### A. Digest-Based & Alias Helm Deployment
To deploy the gateway with absolute immutability, operators should reference the exact SHA256 digest captured in `bytetaper-runtime-image-digest.txt`. Alternatively, operators can target any of the published SemVer aliases (`vX.Y.Z`, `X.Y.Z`) or commit SHAs (`sha-<12>`):

```bash
helm upgrade --install bytetaper ./bytetaper-0.1.0.tgz \
  --set image.repository=ghcr.io/haluan/bytetaper-runtime \
  --set image.tag="0.1.0" \
  --namespace bytetaper --create-namespace
```

### B. Cryptographic Checksum Verification
Operators can verify the integrity of the downloaded release bundle before deployment:
```bash
sha256sum -c bytetaper-checksums.txt
```

### C. SBOM Consumption
The attached CycloneDX and SPDX JSON SBOMs can be directly imported into enterprise Dependency-Track or vulnerability management platforms to maintain continuous compliance tracking.

---

## 4. Rollback Guidance

ByteTaper gateway releases are strictly stateless runtime engines. If a new release exhibits regressions or policy incompatibilities, operators can achieve instantaneous rollback by reverting the Helm release to the prior image digest:

```bash
helm rollback bytetaper <previous-revision-number>
```
Or by explicitly upgrading with the previous version's Helm chart package and image digest.

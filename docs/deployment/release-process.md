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

### A. Production Helm Deployment
To deploy ByteTaper with absolute immutability and high availability, use the packaged chart and pin the OCI image by its top-level manifest-list digest.

**Standard Single-Replica (Persistent Cache):**
```bash
# Recommended for initial production rollout
helm upgrade --install bytetaper ./bytetaper-0.1.0.tgz \
  --set image.repository=ghcr.io/ByteTaper/bytetaper-runtime \
  --set image.digest="sha256:<manifest-list-digest>" \
  --set l2Cache.persistence.enabled=true \
  --set admin.enabled=false \
  --namespace bytetaper --create-namespace
```

**Multi-Replica HA (Zero-Downtime):**
```bash
# Recommended for high-traffic environments
helm upgrade --install bytetaper ./bytetaper-0.1.0.tgz \
  --set replicaCount=3 \
  --set image.digest="sha256:<manifest-list-digest>" \
  --set l2Cache.persistence.enabled=false \
  --set l2Cache.emptyDir.enabled=true \
  --set podDisruptionBudget.enabled=true \
  --namespace bytetaper
```

> [!IMPORTANT]
> **HA Storage Caveat**: Because the current chart uses a `Deployment`, `replicaCount > 1` is only safe when using `emptyDir` storage for the L2 cache. Persistent L2 caching across multiple replicas requires a future StatefulSet implementation. See [helm-ha-production.md](./helm-ha-production.md) for details.

### B. Cryptographic Checksum Verification
Operators can verify the integrity of the downloaded release bundle before deployment:
```bash
sha256sum -c bytetaper-checksums.txt
```

### C. Multi-Architecture Image Usage
ByteTaper releases are published as multi-architecture OCI manifest lists supporting both `linux/amd64` and `linux/arm64`.

**Inspect published manifest:**
```bash
docker buildx imagetools inspect ghcr.io/ByteTaper/bytetaper-runtime:v0.1.0
```

**Run release image locally:**
```bash
docker pull ghcr.io/ByteTaper/bytetaper-runtime:v0.1.0
docker run --rm ghcr.io/ByteTaper/bytetaper-runtime:v0.1.0 --version
```

**Digest Pinning for Multi-Arch:**
The `bytetaper-runtime-image-digest.txt` artifact contains the digest of the top-level manifest list. When pinning `image.digest` in Helm, use this manifest-list digest to ensure your cluster pulls the correct architecture for each node automatically.

### D. SBOM Consumption
The attached CycloneDX and SPDX JSON SBOMs can be directly imported into enterprise Dependency-Track or vulnerability management platforms to maintain continuous compliance tracking.

---

## 4. Rollback Guidance

ByteTaper gateway releases are strictly stateless runtime engines. If a new release exhibits regressions or policy incompatibilities, operators can achieve instantaneous rollback by reverting the Helm release to the prior image digest:

```bash
helm rollback bytetaper <previous-revision-number>
```
Or by explicitly upgrading with the previous version's Helm chart package and image digest.

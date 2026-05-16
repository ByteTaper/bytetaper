# Verifying ByteTaper Release Integrity

Every official ByteTaper runtime image is cryptographically signed and includes attestations for its Software Bill of Materials (SBOM) and build provenance.

## Prerequisites

You must have [cosign](https://github.com/sigstore/cosign) installed (v2.0 or later).

## 1. Locate the Image Digest

It is a security best practice to reference images by their unique SHA256 digest rather than by tag. You can find the digest in the `bytetaper-release-manifest.json` included with each release:

```json
"runtime_image": {
  "digest": "sha256:...",
  "reference": "ghcr.io/haluan/bytetaper-runtime@sha256:..."
}
```

## 2. Verify Signature

ByteTaper uses keyless signing via Sigstore. The signature is bound to the official GitHub Actions release workflow identity.

```bash
IMAGE_REF="ghcr.io/haluan/bytetaper-runtime@sha256:<digest>"

cosign verify \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  --certificate-identity-regexp 'https://github.com/haluan/bytetaper/.github/workflows/release.yml@refs/tags/v.*' \
  "${IMAGE_REF}"
```

**Expected output:**
A JSON array of verification results. If the command exits with `0`, the signature is valid and authentic.

## 3. Verify SBOM Attestations

We attach both CycloneDX and SPDX SBOMs as attestations to the image.

### CycloneDX (Primary)
```bash
cosign verify-attestation --type cyclonedx \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  --certificate-identity-regexp 'https://github.com/haluan/bytetaper/.github/workflows/release.yml@refs/tags/v.*' \
  "${IMAGE_REF}"
```

### SPDX
```bash
cosign verify-attestation --type spdx \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  --certificate-identity-regexp 'https://github.com/haluan/bytetaper/.github/workflows/release.yml@refs/tags/v.*' \
  "${IMAGE_REF}"
```

## 4. Verify SLSA Provenance

Build provenance proves that the image was built by the official ByteTaper release workflow.

```bash
cosign verify-attestation --type slsaprovenance \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  --certificate-identity-regexp 'https://github.com/haluan/bytetaper/.github/workflows/release.yml@refs/tags/v.*' \
  "${IMAGE_REF}"
```

## 5. Pinning the Digest in Helm

When deploying with Helm, always use the `image.digest` value to ensure your cluster pulls exactly the verified bits:

```yaml
# values.yaml
image:
  repository: ghcr.io/haluan/bytetaper-runtime
  tag: v0.1.0
  digest: sha256:8f...
```

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| `cosign verify` fails | Identity mismatch | Ensure the `--certificate-identity-regexp` matches the official repository path. |
| No attestations found | Partial release | Ensure you are using an image from a published `v*` release, not a dry-run or development build. |
| Digest mismatch | Tag re-pushed | Always pull by `@sha256:...` digest to prevent being affected by mutable tags. |

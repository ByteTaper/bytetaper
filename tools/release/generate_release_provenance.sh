#!/bin/bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

BYTETAPER_VERSION="${BYTETAPER_VERSION:-v0.0.0-dev}"
BYTETAPER_GIT_SHA="${BYTETAPER_GIT_SHA:-$(git rev-parse HEAD)}"
BYTETAPER_BUILD_DATE="${BYTETAPER_BUILD_DATE:-$(date -u +%Y-%m-%dT%H:%M:%SZ)}"
GITHUB_REF="${GITHUB_REF:-refs/heads/main}"
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-ByteTaper/bytetaper}"
GITHUB_WORKFLOW="${GITHUB_WORKFLOW:-Release Publishing Pipeline}"
GITHUB_WORKFLOW_REF="${GITHUB_WORKFLOW_REF:-${GITHUB_REPOSITORY}/.github/workflows/release.yml@${GITHUB_REF}}"
GITHUB_RUN_ID="${GITHUB_RUN_ID:-0}"
GITHUB_RUN_ATTEMPT="${GITHUB_RUN_ATTEMPT:-1}"
GITHUB_ACTOR="${GITHUB_ACTOR:-unknown}"
GITHUB_SERVER_URL="${GITHUB_SERVER_URL:-https://github.com}"
BYTETAPER_RUNTIME_OS_IMAGE="${BYTETAPER_RUNTIME_OS_IMAGE:-ubuntu:26.04}"

DIGEST_FILE="dist/release/bytetaper-runtime-image-digest.txt"
PROVENANCE_FILE="dist/release/bytetaper-provenance.json"

if [[ ! -f "${DIGEST_FILE}" ]]; then
  echo "ERROR: Missing digest file ${DIGEST_FILE}"
  exit 1
fi

IMAGE_NAME=$(grep "^image=" "${DIGEST_FILE}" | cut -d= -f2- | xargs)
IMAGE_DIGEST=$(grep "^digest=" "${DIGEST_FILE}" | cut -d= -f2- | xargs)
IMAGE_REF=$(grep "^reference=" "${DIGEST_FILE}" | cut -d= -f2- | xargs)
IMAGE_DIGEST_HEX="${IMAGE_DIGEST#sha256:}"

if ! [[ "${IMAGE_DIGEST}" =~ ^sha256:[0-9a-f]{64}$ ]]; then
  echo "ERROR: Invalid image digest '${IMAGE_DIGEST}' in ${DIGEST_FILE}"
  exit 1
fi

jq -n \
  --arg builder_id "${GITHUB_SERVER_URL}/${GITHUB_WORKFLOW_REF}" \
  --arg repo_uri "git+${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}.git" \
  --arg repo_url "${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}" \
  --arg git_sha "${BYTETAPER_GIT_SHA}" \
  --arg git_ref "${GITHUB_REF}" \
  --arg version "${BYTETAPER_VERSION}" \
  --arg build_date "${BYTETAPER_BUILD_DATE}" \
  --arg image_name "${IMAGE_NAME}" \
  --arg image_ref "${IMAGE_REF}" \
  --arg image_digest "${IMAGE_DIGEST}" \
  --arg image_digest_hex "${IMAGE_DIGEST_HEX}" \
  --arg os_image "${BYTETAPER_RUNTIME_OS_IMAGE}" \
  --arg workflow "${GITHUB_WORKFLOW}" \
  --arg workflow_ref "${GITHUB_WORKFLOW_REF}" \
  --arg run_id "${GITHUB_RUN_ID}" \
  --arg run_attempt "${GITHUB_RUN_ATTEMPT}" \
  --arg actor "${GITHUB_ACTOR}" \
  '{
    builder: { id: $builder_id },
    buildType: "https://github.com/ByteTaper/bytetaper/actions/workflows/release.yml",
    invocation: {
      configSource: {
        uri: $repo_uri,
        digest: { sha1: $git_sha },
        entryPoint: ".github/workflows/release.yml"
      },
      parameters: {
        version: $version,
        image: $image_name,
        image_reference: $image_ref,
        dockerfile: "docker/production.Dockerfile",
        os_image: $os_image,
        platforms: ["linux/amd64", "linux/arm64"]
      },
      environment: {
        github_repository: "ByteTaper/bytetaper",
        github_ref: $git_ref,
        github_workflow: $workflow,
        github_workflow_ref: $workflow_ref,
        github_run_id: $run_id,
        github_run_attempt: $run_attempt,
        github_actor: $actor,
        runner_image: "ubuntu-26.04"
      }
    },
    metadata: {
      buildInvocationId: ("https://github.com/ByteTaper/bytetaper/actions/runs/" + $run_id + "/attempts/" + $run_attempt),
      buildStartedOn: $build_date,
      buildFinishedOn: $build_date,
      reproducible: false
    },
    materials: [
      { uri: $repo_uri, digest: { sha1: $git_sha } },
      { uri: $repo_url, digest: { sha1: $git_sha } },
      { uri: $image_ref, digest: { sha256: $image_digest_hex } }
    ],
    buildConfig: {
      project: "bytetaper",
      component: "bytetaper-runtime",
      release_manifest: "bytetaper-release-manifest.json",
      sbom_artifacts: ["bytetaper-sbom.cdx.json", "bytetaper-sbom.spdx.json"],
      vulnerability_report_artifacts: ["bytetaper-vulnerability-report.json", "bytetaper-vulnerability-report.sarif"],
      os_image: $os_image,
      image_digest: $image_digest
    }
  }' > "${PROVENANCE_FILE}"

jq -e '.builder.id and .buildType and .invocation.configSource and .metadata.buildInvocationId and (.materials | length > 0)' \
  "${PROVENANCE_FILE}" >/dev/null

echo "SLSA provenance predicate generated: ${PROVENANCE_FILE}"

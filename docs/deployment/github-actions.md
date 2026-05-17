# GitHub Actions Deployment Runbook

This runbook guides CI/CD engineers through configuring automated staging and production deployment pipelines for ByteTaper using the reusable GitHub Actions templates (`BT-DIST-003`).

## 1. Pipeline Architecture

The workflow templates located under `deploy/github-actions/` serve as canonical starting points. Operators should copy the desired template into their repository's `.github/workflows/` directory and adapt inputs or environment triggers as needed.

### Example: Adapting `deploy-helm-digest.yml`
Copy `deploy/github-actions/deploy-helm-digest.yml` to `.github/workflows/deploy-bytetaper.yml` in your deployment repository. Operators can then trigger deployments via `workflow_dispatch` or automate them on tag push events:

```yaml
name: Strict Digest Deploy ByteTaper

on:
  workflow_dispatch:
    inputs:
      image_digest:
        description: "Pinned SHA256 image digest (must start with sha256:)"
        required: true
      chart_version:
        description: "Helm chart version to deploy"
        required: true
        default: "0.1.0"

jobs:
  deploy:
    runs-on: ubuntu-26.04
    environment: production
    steps:
      - name: Checkout deployment repository
        uses: actions/checkout@v6

      - name: Setup Helm
        uses: azure/setup-helm@v4
        with:
          version: v3.17.0

      - name: Validate image digest input
        run: |
          DIGEST="${{ inputs.image_digest }}"
          if [[ -z "${DIGEST}" || ! "${DIGEST}" =~ ^sha256:[0-9a-f]{64}$ ]]; then
            echo "ERROR: Invalid image_digest input: '${DIGEST}'."
            exit 1
          fi

      - name: Configure Kubernetes credentials
        run: |
          mkdir -p ~/.kube
          echo "${{ secrets.KUBECONFIG }}" > ~/.kube/config
          chmod 600 ~/.kube/config

      - name: Deploy Helm Chart
        run: |
          helm upgrade --install bytetaper charts/bytetaper \
            --namespace bytetaper --create-namespace \
            --set image.digest="${{ inputs.image_digest }}" \
            --version "${{ inputs.chart_version }}"
```

## 2. Environment Protection Configuration

To ensure continuous deployment compliance, repository administrators must configure target environment protection rules:

1. Navigate to **Settings > Environments > production**.
2. Enable **Required reviewers** and assign responsible infrastructure operators.
3. Configure **Concurrency limits** (Limit to 1 concurrent job) to prevent deployment race conditions.
4. Add the cluster `KUBECONFIG` secret exclusively to the protected environment scope.

## 3. Deployment Validation Workflow

The `validate-helm-deployment.yml` workflow template provides automated smoke testing immediately following deployment rollouts. When copied to your repository, it verifies:
* Pod Scheduling and Container Liveness/Readiness probes.
* Prometheus metrics endpoint reachability (`http://localhost:18081/metrics`).

## 4. Rollback Execution

If a deployment rollout introduces latency regressions or policy errors, operators can trigger instantaneous rollback using an adapted `rollback-helm.yml` workflow:

1. Open **Actions > Rollback ByteTaper Helm Release**.
2. Click **Run workflow**.
3. Input target `namespace`, `release_name`, and the target stable `revision` number.
4. Confirm execution. The workflow automatically awaits and verifies the rollback rollout status.

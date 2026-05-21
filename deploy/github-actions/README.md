# ByteTaper GitHub Actions Deployment Templates

This directory contains official, reusable GitHub Actions deployment workflows for managing ByteTaper Helm releases across staging and production environments.

## Available Workflows

| Template | Purpose | Key Inputs |
|---|---|---|
| `deploy-helm.yml` | Standard Helm upgrade/install pipeline. | `image_digest`, `chart_version`, `namespace` |
| `deploy-helm-digest.yml` | Strict deployment pinning verifying exact SHA256 formatting. | `image_digest` (strict regex) |
| `rollback-helm.yml` | Instantaneous rollback to prior Helm revision. | `revision`, `release_name` |
| `validate-helm-deployment.yml` | Operational readiness check probing health and metrics endpoints. | `namespace`, `release_name` |

## Required Secrets
* `KUBECONFIG`: Base64 encoded or raw `kubeconfig` granting deployment credentials to the target Kubernetes cluster.

## Environment Protection Rules
All production deployment templates target the `production` GitHub Actions environment. Organizations should configure manual approval gates and concurrency limits within repository settings to protect production deployments.

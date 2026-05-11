# Route Policy

Route Policy is the central configuration framework in ByteTaper that determines how specific API requests are optimized based on path, method, and policy rules.

## Description

ByteTaper uses a static, YAML-driven policy engine to map incoming Envoy ExtProc requests to optimization strategies. Each policy is identified by a unique ID and contains matching criteria (like URI prefixes) and enforcement rules (like field selection or response size limits). The system is designed for high performance, utilizing a static configuration model that is validated at startup to ensure safe and predictable runtime behavior.

## How to Setup

Route policies are defined in a YAML configuration file (typically `bytetaper-policy.yaml`) and loaded into the ByteTaper adapter.

1.  **Define a Policy File**: Create a YAML file with a top-level `routes` key.
2.  **Configure Matchers**: Use `kind: "prefix"` or `kind: "exact"` to target specific URIs.
3.  **Set Enforcements**: Add optional blocks for `field_filter`, `max_response_bytes`, or `cache`.
4.  **Validate**: Use the `bytetaper-validate-policy` tool to check your configuration for errors.

Example:

```yaml
routes:
  - id: "v1-api-catch-all"
    match:
      kind: "prefix"
      prefix: "/api/v1"
    method: "any"
    mutation: "disabled"
```

## Behavior Reference

| Example Scenario | Policy Configuration | Expected Result |
| :--- | :--- | :--- |
| **Match any subpath** | `kind: "prefix"`, `prefix: "/api"` | Matches `/api/users`, `/api/v1/status`, etc. |
| **Match specific endpoint** | `kind: "exact"`, `prefix: "/v1/health"` | Matches only `/v1/health`; ignores `/v1/health/check`. |
| **Limit HTTP Methods** | `method: "get"` | Policy only applies to GET requests; others pass through unchanged. |
| **Enforce Payload Cap** | `max_response_bytes: 524288` | Responses larger than 512KB will be flagged or rejected. |
| **Disable Processing** | `mutation: "disabled"` | ByteTaper classifies the request but skips all transformation logic. |

> [!TIP]
> **Priority**: Route matching is performed in the order defined in the YAML file. The first route that matches the incoming request will be applied.

## Coalescing

Request coalescing reduces upstream fan-out by queuing followers behind a single leader request. See [docs/coalescing-policy.md](coalescing-policy.md) for the full field reference, budget model, and guardrail configuration.

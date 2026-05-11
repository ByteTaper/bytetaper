# Request Coalescing Policy

Request Coalescing in ByteTaper allows you to deduplicate concurrent, identical GET requests to your upstream services. Instead of sending multiple requests for the same resource simultaneously, ByteTaper "parks" duplicate requests and satisfies them with a single upstream response, significantly reducing upstream load during traffic bursts.

## Configuration

Request coalescing is configured per-route in the ByteTaper policy YAML.

```yaml
routes:
  - id: "example-cached-route"
    match:
      prefix: "/api/v1/cached/"
    cache:
      enabled: true
      ttl_seconds: 300
    coalescing:
      enabled: true                          # Enable coalescing for this route
      mode: "cache_assisted"                 # Only "cache_assisted" is supported
      backend_timeout_ms: 500                # Expected max upstream latency for leader request
      handoff_buffer_ms: 250                 # Notification and wakeup buffer added to budget
      result_ready_retention_ms: 50          # Late follower registry entry retention time
      max_waiters_per_key: 64                # Max concurrent followers per unique request key
      require_cache_enabled: true            # Must be true (cache acts as response delivery vehicle)
      allow_authenticated: false             # Whether to coalesce requests with Authorization headers
      max_follower_wait_budget_ms: 1000      # Hard cap on effective wait budget (0 = disabled)
      max_active_follower_waiters: 2048      # Global concurrent follower waiter cap (0 = disabled)
      max_active_follower_waiters_per_shard: 64 # Per-shard concurrent follower waiter cap (0 = disabled)
```

### Field Reference

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | boolean | `false` | Enables request coalescing for the route. |
| `mode` | string | `"cache_assisted"` | The request coalescing strategy. Only `"cache_assisted"` is supported. |
| `backend_timeout_ms` | integer | `500` | Expected maximum upstream latency for the leader request. |
| `handoff_buffer_ms` | integer | `250` | Extra duration added to the budget to cover notification dispatch and follower wakeup latency. |
| `result_ready_retention_ms` | integer | `50` | How long a completed in-flight registry entry remains available for late-arriving followers. |
| `max_waiters_per_key` | integer | `64` | The maximum number of concurrent requests that can be parked for a single unique request key. If exceeded, new requests will bypass coalescing. |
| `require_cache_enabled` | boolean | `true` | Coalescing requires a cache to be enabled on the route to distribute the response safely. |
| `allow_authenticated` | boolean | `false` | If `true`, requests with an `Authorization` header will be coalesced. Use with extreme caution as this can leak private data if the upstream does not vary responses by token. |
| `max_follower_wait_budget_ms` | integer | `0` | Guardrail: Bypasses wait if computed total wait budget exceeds this threshold (0 = disabled). |
| `max_active_follower_waiters` | integer | `0` | Guardrail: Bypasses wait if global concurrent active follower waiters reach this count (0 = disabled). |
| `max_active_follower_waiters_per_shard` | integer | `0` | Guardrail: Bypasses wait if per-shard concurrent active follower waiters reach this count (0 = disabled). |

---

## Wait Budget Explanation

The total wait budget for a follower is calculated dynamically based on your configured latency assumptions:

$$\text{follower\_wait\_budget\_ms} = \text{backend\_timeout\_ms} + \text{handoff\_buffer\_ms}$$

- **`backend_timeout_ms`**: Should be sized close to the upstream p95 or p99 response time for the endpoint.
- **`handoff_buffer_ms`**: Covers notification signaling, worker scheduling overhead, and gRPC stream waking.
- **`result_ready_retention_ms`**: This is **NOT** cache TTL. It controls how long the finished leader's response metadata resides in the in-flight registry memory. This enables late-arriving followers (who arrived *just* as the leader completed) to be routed to the cache instead of triggering another upstream call. Set this proportional to `handoff_buffer_ms`.

---

## Guardrails

Guardrail fields protect the proxy from memory exhaustion and cascading delays under slow upstreams or heavy load spikes:

- **`max_follower_wait_budget_ms`**: If the computed budget exceeds this value, followers will bypass waiting and proceed to the backend directly.
- **`max_active_follower_waiters`**: Sets a hard cap on global concurrent parked follower streams in the system.
- **`max_active_follower_waiters_per_shard`**: Avoids thread contention and hotspots by restricting parked streams inside a single hashing registry partition.

When any of these guardrail limits are reached, the follower request bypasses the coalescing wait stage completely. This bypass is recorded in specific guardrail Prometheus metrics.

---

## Migration from `wait_window_ms`

The single `wait_window_ms` field is **deprecated** but fully supported for backward compatibility:

- **Implicit Derivation**: If only `wait_window_ms` is supplied, ByteTaper automatically derives the budget parameters to preserve the previous fallback window duration:
  $$\text{backend\_timeout\_ms} = \text{wait\_window\_ms}$$
  $$\text{handoff\_buffer\_ms} = \frac{\text{wait\_window\_ms}}{2}$$
  $$\text{result\_ready\_retention\_ms} = \frac{\text{wait\_window\_ms}}{4}$$
  A deprecation warning is written to logs and the startup policy report.
- **Validation Rejection**: Setting both `wait_window_ms` and any of the new budget fields (e.g., `backend_timeout_ms`) on the same route coalescing configuration is invalid and will cause policy loading to fail.

### Recommended Migration

Replace `wait_window_ms: N` with:
```yaml
coalescing:
  enabled: true
  backend_timeout_ms: N
  handoff_buffer_ms: N / 2          # (Integer division)
  result_ready_retention_ms: N / 4  # (Integer division)
```

---

## How It Works

### Leader/Follower Model

1. **Leader**: The first request for a unique resource (defined by URI and headers) becomes the **Leader**. ByteTaper registers this request in an internal in-flight registry and forwards it to the upstream.
2. **Follower**: Subsequent concurrent requests for the same resource become **Followers**. ByteTaper "parks" gRPC stream threads at the Envoy edge, holding them without sending duplicate upstream calls.
3. **Completion**: When the Leader's response returns, it is stored in the cache. ByteTaper then notifies the parked Followers. They perform an immediate cache lookup, find the response, and write it back to their respective clients.

### Safety & Constraints

- **HTTP Method**: Coalescing is strictly limited to **GET** requests. Mutations (POST, PUT, DELETE, etc.) are never coalesced.
- **Authentication**: By default, requests with an `Authorization` header bypass coalescing (`allow_authenticated: false`). This prevents identity/token leaks.

---

## Prometheus Metrics

ByteTaper exports the following Prometheus metrics for monitoring request coalescing health:

| Metric | Type | Description |
|--------|------|-------------|
| `bytetaper_coalescing_leader_total` | Counter | Total number of requests that acted as leaders. |
| `bytetaper_coalescing_follower_total` | Counter | Total number of requests parked as followers. |
| `bytetaper_coalescing_fallback_total` | Counter | Total number of followers that timed out and fell back to upstream. |
| `bytetaper_coalescing_max_waiters_exceeded_total` | Counter | Number of requests that bypassed coalescing because the `max_waiters_per_key` limit was reached. |
| `bytetaper_coalescing_follower_wait_guardrail_budget_exceeded_total` | Counter | Followers that bypassed coalescing because the wait budget exceeded `max_follower_wait_budget_ms`. |
| `bytetaper_coalescing_follower_wait_guardrail_global_limit_total` | Counter | Followers that bypassed coalescing because total active waiters reached `max_active_follower_waiters`. |
| `bytetaper_coalescing_follower_wait_guardrail_shard_limit_total` | Counter | Followers that bypassed coalescing because shard active waiters reached `max_active_follower_waiters_per_shard`. |
| `bytetaper_coalescing_active_follower_waiters` | Gauge | Current number of active follower requests currently parked and waiting in the registry. |

## API Intelligence

ByteTaper records "Coalescing Opportunities" when it detects bursts of duplicate GET requests on routes where coalescing is disabled. This data can be used by ByteTaper's intelligence layer to recommend enabling coalescing for specific high-traffic endpoints.

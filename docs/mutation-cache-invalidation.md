# Mutation-Aware Cache Invalidation

## Overview

ByteTaper caches `GET` responses to reduce upstream load and latency. Mutation methods (`PATCH`, `PUT`, `DELETE`) are not cache-admitted (they never store their responses in the cache). When a mutation route is configured with an explicit **invalidation policy**, ByteTaper prepares an invalidation plan during request processing and applies it only after a successful upstream response is received.

## Why Invalidation is Explicit

ByteTaper does not automatically invalidate cache entries based on path similarity for several reasons:
- **Domain Logic**: A mutation to `/api/v1/users/123` might affect `GET /api/v1/users/123`, but it could also affect `GET /api/v1/users`, `GET /api/v1/teams/55/users`, or even an analytics endpoint.
- **Fan-out**: One mutation can affect multiple read routes.
- **Performance**: Automatic global invalidation or wide-range RocksDB scans are too expensive to perform on every mutation.
- **Safety**: A "no-op by default" approach ensures that cache behavior is predictable and does not cause unexpected side effects in complex routing setups.

## Supported Methods

- **Triggers**: `PATCH`, `PUT`, `DELETE`.
- **Unsupported**: `GET` (read-only), `POST` (typically creates new resources, often doesn't invalidate existing collections in simple v1 models).

## Policy Model (YAML Example)

```yaml
routes:
  - id: get_user
    match: { kind: exact, prefix: /api/v1/users/{id} }
    method: get
    cache:
      enabled: true
      behavior: store
      ttl_seconds: 60
      layers:
        l1: { enabled: true }
        l2: { enabled: true, path: /var/lib/bytetaper/cache/users }

  - id: update_user
    match: { kind: exact, prefix: /api/v1/users/{id} }
    method: patch
    cache:
      invalidation:
        enabled: true
        on_methods: [PATCH]
        timing: after_successful_upstream_response
        success_status: { min: 200, max: 299 }
        targets:
          - route_id: get_user
            strategy: route_epoch
```

## Success Status Semantics

Invalidation is only applied if the upstream response status code falls within the configured `success_status` range (default: 200-299).

| Upstream Status | Behavior |
|---|---|
| 200 OK, 204 No Content | Apply invalidation |
| 404 Not Found, 400 Bad Request | Skip (data didn't change) |
| 500 Internal Server Error | Skip (safer to assume no change) |

## Route Cache Epoch Invalidation

ByteTaper uses a logical **Route Cache Epoch** for invalidation. Each cacheable route has an atomic integer (the epoch) stored in memory.

- Every `CacheKey` includes the current epoch for its route.
- Bumping the epoch (e.g., from 7 to 8) makes all existing cache entries for that route **logically unreachable** because their keys (containing `epoch:7`) will no longer match the new lookup keys (containing `epoch:8`).
- **Policy Identity vs. Runtime Epoch**: The `policy-sha` or identity of a route remains stable even as its data is invalidated. The route epoch is a runtime generation counter that changes to force a cache refresh without needing to re-deploy or change the policy itself.
- Old entries remain physically in the L1 ring buffer or L2 disk cache but are ignored. They are eventually overwritten by new data or removed by TTL/cleanup processes.

## Raw and Variant Cache Consistency

Materialized field-filtered variant cache entries (`var:` keys) also include the route epoch. When a mutation bumps the route epoch, both the raw response and all its variants are simultaneously invalidated.

## L1 Behavior

- **Logical Removal**: The epoch bump immediately invalidates all L1 entries for the target route.
- **Physical Removal**: For known specific keys, `l1_remove()` can be used to free slots immediately, but this is usually not required as the ring-buffer will naturally overwrite old entries.

## L2 Behavior

- **Correctness via Epoch**: L2 invalidation relies exclusively on the route epoch for correctness in v1.
- **Physical Removal**: While infrastructure for asynchronous disk deletion (`l2_remove()`) exists via the worker queue for explicit maintenance jobs, mutation-triggered invalidation currently uses the Route Epoch bump for immediate logical consistency.
- **Stale Safety**: Because route epochs are stored in memory, they reset on process restart. Persistent L2 entries should have a reasonable TTL to prevent long-term accumulation of stale data after a restart.

## Strategy Reference

| Strategy | v1 Status | Description |
|---|---|---|
| `route_epoch` | **Supported** | Bumps in-memory atomic counter. Immediate, fast, and the primary correctness mechanism. |
| `exact_key` | Not Accepted | Not currently supported for mutation hot-path invalidation. Primitives exist for background maintenance. |
| `prefix` | Not Supported | Requires an expensive scan of the cache; prohibited on the mutation hot path. |

## Metrics and Observability

Monitor these counters to verify invalidation health:
- `bytetaper_cache_invalidation_prepare_attempt_total`
- `bytetaper_cache_invalidation_prepared_total`
- `bytetaper_cache_invalidation_applied_total`
- `bytetaper_cache_invalidation_skipped_total`
- `bytetaper_cache_invalidation_failed_total`
- `bytetaper_cache_route_epoch_bump_success_total`
- `bytetaper_cache_l2_remove_enqueued_total`
- `bytetaper_cache_l2_remove_success_total`

### Invalidation Reason Strings

When debugging via logs or traces, these strings identify the outcome of an invalidation attempt:

- `prepared`: Invalidation plan created and waiting for upstream response.
- `non-mutation-method`: Request is not PATCH, PUT, or DELETE.
- `no-invalidation-policy`: Route does not have invalidation enabled.
- `method-not-enabled`: Invalidation policy exists but not for this specific method.
- `status-not-successful`: Upstream status code was outside the success range.
- `epoch-store-missing`: Target route does not have a registered epoch counter.
- `epoch-bumped`: Route epoch was successfully incremented.
- `epoch-bump-failed`: Atomic increment of the epoch counter failed.
- `target-cache-disabled`: Target route no longer has caching enabled.
- `unsupported-strategy`: Target strategy is not `route_epoch`.

## Operator Verification Runbook

1. **Warm the Cache**:
   ```bash
   curl -i 'http://localhost:10000/api/v1/users/123?fields=id,name'
   ```
2. **Verify Hit**: Repeat the request and check the `x-bytetaper-cache` header (should show `HIT`).
3. **Perform Mutation**:
   ```bash
   curl -i -X PATCH 'http://localhost:10000/api/v1/users/123' \
     -H 'content-type: application/json' -d '{"name":"new-name"}'
   ```
4. **Verify Invalidation**: The next `GET` request must return fresh data and show a cache `MISS`.
   ```bash
   curl -i 'http://localhost:10000/api/v1/users/123?fields=id,name'
   ```
5. **Check Metrics**:
   ```bash
   curl -s 'http://localhost:18081/metrics' | grep bytetaper_cache_invalidation
   ```

## Troubleshooting

| Symptom | Potential Cause | Check |
|---|---|---|
| Mutation does not invalidate | Policy missing or disabled | `cache.invalidation.enabled` and `on_methods` |
| 500 status, cache unchanged | Invalidation only applies to success | Upstream status code vs `success_status` range |
| Variant response stale | Variant key missing route epoch | Inspect `var:` keys for `epoch:{N}` segment |
| L2 has old physical keys | Epoch is logical; deletion is async | TTL expiry or background cleanup worker |
| Invalidation fails | Target route or epoch store missing | `targets[].route_id` and process start logs |
| Target rejected | Target not a cacheable GET route | `allowed_method: get` and `behavior: store` |
| POST not invalidating | POST is unsupported in v1 | Change method to PATCH, PUT, or DELETE |

## Safety Rules

1. **Never** invalidate before the upstream mutation has succeeded.
2. **Never** perform synchronous disk scans or mass deletions on the mutation hot path.
3. **Always** use the Route Epoch as the primary correctness boundary for wide-scale invalidation.
4. **Never** rely on physical L2 deletion for immediate data consistency.

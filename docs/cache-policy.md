# Cache Policy

ByteTaper provides a high-performance, tiered caching system designed to reduce upstream latency and minimize redundant transformations.

## Architecture

The caching system is divided into two primary layers:

1.  **L1 Cache (Memory)**: A zero-allocation, ring-buffer-based memory cache for ultra-low latency lookups.
2.  **L2 Cache (Disk)**: A persistent storage layer backed by RocksDB for long-term data retention across restarts.

## Configuration Schema

Caching is configured per-route within the `cache` block of a route policy.

```yaml
cache:
  enabled: true
  behavior: store
  ttl_seconds: 3600
  layers:
    l1:
      enabled: true
      capacity_entries: 128
    l2:
      enabled: true
      path: "/var/cache/bytetaper/global"
  private_cache: false
  auth_scope_header: "Authorization"
  vary_headers:
    - "Accept-Language"
    - "X-Client-Platform"
```

### Fields

| Field | Type | Description |
| :--- | :--- | :--- |
| `enabled` | boolean | Globally enables or disables caching for this route. |
| `behavior` | enum | `store` (active caching), `bypass` (ignore cache), `default` (pass-through). |
| `ttl_seconds` | integer | Time-to-live for cached entries in seconds. |
| `layers.l1.enabled` | boolean | Enables the L1 memory layer. |
| `layers.l1.capacity_entries` | integer | Maximum entries in L1 (0 uses system default). |
| `layers.l2.enabled` | boolean | Enables the L2 disk layer. |
| `layers.l2.path` | string | Filesystem path for the RocksDB database (L2 only). |
| `private_cache` | boolean | Allows caching of requests containing authentication headers. |
| `auth_scope_header` | string | Header used to partition the cache (e.g., `Authorization`). |
| `vary_headers` | list of strings | Optional list of headers to generate distinct cache key variants (max 8 headers). |

## Header Variance (Cache Vary)

For APIs that serve multi-tenant or multi-representation content depending on specific request headers (such as `Accept-Language`, `Accept-Encoding`, or platform-specific custom headers), ByteTaper provides an ultra-high performance Cache Vary partitioning implementation.

### How it Works

1. **Header Identification**: During the request-processing phase, the cache prepare stage extracts the values of the headers defined in the `vary_headers` array of the matched route policy.
2. **Cache Key Variant Generation**: These extracted values are mapped alongside their header names directly into the computed Cache Key buffer, isolating the variants.
3. **Collision Isolation Sentinels**:
   - **Missing Headers**: If a configured vary header is completely absent from the client request, it is resolved to a special, unique `<missing>` sentinel hash.
   - **Empty Headers**: If a vary header is present but possesses an empty string value, it is resolved to a special `<empty>` sentinel hash.
   - This ensures strict collision safety, preventing cross-variant cache poisoning.

### Validation Constraints

- **Capacity Limit**: Up to **8** distinct vary headers can be configured per route policy. Duplicates in the list are automatically ignored.
- **Name Constraints**: Each vary header name must be string-scalar, non-empty, and must not exceed **64 characters** in length.
- **Normalization**: All configured header names are automatically normalized to lowercase internally to prevent case-based lookup discrepancies.

## Tiered Cache Lifecycle

1.  **Lookup**:
    -   ByteTaper first checks the **L1 Cache**.
    -   If L1 misses, it checks the **L2 Cache**.
    -   If L2 hits, the entry is **promoted** back to L1 for faster subsequent access.
2.  **Store**:
    -   Upon a successful transformation, the result is stored in both L1 and L2 simultaneously.
    -   L1 uses a ring buffer to evict the oldest entries once capacity is reached.

## Cache Safety & Security

### 1. Authenticated Routes
By default, ByteTaper **disables caching** for any request that includes an `Authorization` or `Cookie` header. This prevents sensitive data from being served to the wrong user.

To enable caching for authenticated routes, you must explicitly set `private_cache: true` and specify an `auth_scope_header`.

### 2. Cache Partitioning (Auth Scope)
When `private_cache` is enabled, the cache key is automatically extended to include the value of the `auth_scope_header`.
- This ensures that User A's cached response is never served to User B, even for the same URL.
- Recommended headers: `Authorization`, `X-User-ID`, or `X-API-Key`.

### 3. Expiry Check
Every cache hit is validated against the `ttl_seconds` policy. If an entry is found but has expired, it is treated as a miss and deleted from the cache.

## Performance Considerations

- **L1 Capacity**: Keep L1 capacity within reasonable memory limits. Each entry consumes approximately 64KB + metadata.
- **L2 Disk I/O**: Use fast storage (NVMe/SSD) for the L2 path to ensure promotion latency remains negligible.
- **Immediate Response**: On a cache hit, ByteTaper returns the response **immediately**, bypassing the expensive transformation pipeline.

## Mutation-aware Invalidation

ByteTaper supports explicit cache invalidation triggered by mutation requests (`PATCH`, `PUT`, `DELETE`). This ensures that cached `GET` responses are refreshed when the underlying data changes.

For detailed configuration and behavior, see [Mutation-Aware Cache Invalidation](mutation-cache-invalidation.md).

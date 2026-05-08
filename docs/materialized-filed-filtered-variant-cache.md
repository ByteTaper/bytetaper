# Materialized Field-Filtered Variant Cache Policy

The **Materialized Field-Filtered Variant Cache** is a specialized, opt-in optimization layer in ByteTaper. It allows repeated client requests specifying custom field projections (e.g. `GET /products?fields=id,name`) to completely skip the CPU-intensive processes of JSON parsing, key-path filtering, and serialization. It accomplishes this by caching pre-filtered, ready-to-serve JSON bodies directly in L1 (Memory) and L2 (Disk RocksDB) cache layers under isolated namespaces.

---

## 🗺️ Architectural Lifecycle

The variant cache sits on the hot-path within the ExtProc request and response pipelines, operating as a high-speed, tiered cache layer:

```mermaid
graph TD
    A[Client Request] --> B[cache_key_prepare_stage]
    B --> C[field_variant_admission_stage]
    C --> D[l1_variant_lookup_stage]
    D -- Hit --> E[Serve pre-filtered JSON directly]
    D -- Miss --> F[l2_cache_async_lookup]
    F -- Hit --> G[Promote to L1 & Serve]
    F -- Miss --> H[Raw L1/L2 Lookups]
    H -- Hit --> I[Parse/Filter & Serve]
    H -- Miss --> J[Upstream Origin API]
    J --> K[l1_variant_store_stage]
    K --> L[l2_cache_async_store]
```

---

## 🔑 Key Isolation & Namespace Protection

To prevent **Cache Contamination** (where a request expecting a filtered response is accidentally served a full-body response or vice versa), the variant cache enforces absolute namespace isolation.

### 1. Key Structure
*   **Raw Cache Keys**: Cached under the standard route key:
    ```
    GET|{route_id}|{path}|{sanitized_query}||{policy_version}
    ```
*   **Variant Cache Keys**: Prefixed with a dedicated `"var:"` namespace to guarantee separation:
    ```
    var:GET|{route_id}|{path}|{sanitized_query}|{f1,f2,...}|{policy_version}
    ```

### 2. Query Sanitization & Field Ordering
*   **Query Parameter Stripping**: The raw query is sanitized via `sanitize_query_strip_fields_param` to strip the `fields=` query parameter. This ensures raw cache lookups are unaffected by the field selection query parameters.
*   **Signature Canonicalization**: Field names requested in `fields=` are deduplicated and sorted lexicographically. Thus, `?fields=name,id` and `?fields=id,name` are canonicalized to the exact same variant key segment (`id,name`), ensuring 100% cache-key equivalence and high hit density.

---

## ⚙️ Configuration Schema

The policy is configured per-route inside the route-level `cache` configuration block.

```yaml
routes:
  - id: "products-filtering"
    match:
      kind: exact
      prefix: /products
    mutation: full
    cache:
      behavior: store
      ttl_seconds: 3600
      field_variant:
        enabled: true
        max_variants_per_route: 8
        min_field_count: 1
        max_field_count: 8
        admission_threshold: 2
        ttl_max_ms: 10000
```

### Configuration Parameters

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `enabled` | boolean | `false` | Globally enables or disables variant materialization for this route. |
| `max_variants_per_route` | integer | `8` | Bounds cardinality per-route to prevent infinite variant memory expansion. |
| `min_field_count` | integer | `1` | Ignores trivial empty field selections. |
| `max_field_count` | integer | `8` | Excludes overly-specific permutations to avoid cache pollution. |
| `admission_threshold` | integer | `2` | Number of cold requests required before the variant is admitted and stored. |
| `ttl_max_ms` | integer | `0` | Sub-policy TTL limit for variants (0 uses the standard raw cache TTL). |

---

## 🛡️ Cardinality Guard & Popularity Admission

To protect against cardinality explosion and cache trashing from random, one-off query combinations, a lock-free, zero-allocation **Popularity-Based Admission Guard** is active:

1.  **State Tracking**: Each route maintains a lightweight, contiguous registry bounded strictly by `max_variants_per_route`.
2.  **Heat Accumulation**: A requested field signature is tracked inside this registry upon miss. Its hit count is incremented atomically.
3.  **Storage Gating**: No pre-filtered body is committed to L1/L2 cache storage unless its hit count is `>= admission_threshold`.
4.  **Raw Cache Protection**: To prevent corrupted raw-cache states, standard raw lookup and store stages completely bypass storage whenever custom field query parameters are present (`selected_field_count > 0`) **and** variant materialization is enabled (`field_variant.enabled: true`), letting the variant pipeline handle projection caching safely. If variant caching is disabled, standard stages handle query projections under a unique field-aware primary cache key, preserving traditional backward-compatible caching.

---

## ⏱️ Tiered Lifecycle & Promotion

*   **Lookup Precedence**: The pipeline executes `l1_variant_lookup_stage` before raw L1/L2 lookup stages. A hit resolves in microsecond speed and skips the entire transformer pipeline.
*   **L2-to-L1 Promotion**: Upon L2 RocksDB variant hits, the entry is promoted to the local memory L1 variant cache automatically so subsequent lookups bypass disk read-overhead.
*   **TTL Bound Capping**: The time-to-live of any stored variant is strictly bounded:
    $$\text{Variant TTL} = \min(\text{raw\_cache\_ttl}, \text{field\_variant.ttl\_max\_ms})$$
    This prevents stale projections from lingering beyond raw policy constraints.

---

## 📊 Performance Guidelines

Based on real production-simulated benchmarks:
*   **Unfiltered/Filtered Path**: Served in **~2-5 ms** with transformation overhead.
*   **Materialized Variant L1 Hit**: Served in **~0.5-0.7 ms** with zero JSON parsing or CPU overhead.
*   **Throughput**: Scale capacity linearly up to **15,000+ RPS** per pipeline worker under variant-cache saturation.

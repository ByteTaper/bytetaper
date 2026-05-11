# Compiled Route Runtime

At startup, ByteTaper compiles a fixed stage array for each route defined in the configuration. Per-request execution is a direct, zero-branching array traversal over these pre-assembled arrays.

This design avoids testing feature flags on every single request, keeping the hot path stage-linear, minimizing cache misses, and ensuring pipeline composition is completely deterministic and auditable.

---

## Why Routes Are Compiled

By pre-calculating the pipeline array during initialization, we avoid the overhead of checking feature flags (`policy.cache.behavior`, `policy.coalescing.enabled`, etc.) on every request:
- **Zero-Branching**: Request-header, response-header, and response-body stages are stored as sequential pointer arrays inside `CompiledRouteRuntime`.
- **Fast Traversal**: The execution engine iterates over these contiguous arrays linearly.
- **Auditable Safety**: Route pipelines are constructed deterministically by a single function (`compile_route_runtime()`), making it easy to audit which capabilities are active.

---

## Stage Pipelines

Each route has three arrays: lookup (request-header), store (response-body), and response (response-header).

| Stage Name | Pipeline | Conditional On |
|---|---|---|
| `cache_key_prepare_stage` | Lookup | `cache.behavior == Store` |
| `field_variant_admission_stage` | Lookup | `cache.behavior == Store` AND `cache.field_variant.enabled == true` |
| `l1_cache_lookup_stage` | Lookup | `cache.behavior == Store` |
| `l1_variant_lookup_stage` | Lookup | `cache.behavior == Store` AND `cache.field_variant.enabled == true` |
| `coalescing_decision_stage` | Lookup | `coalescing.enabled == true` |
| `coalescing_follower_wait_stage` | Lookup | `coalescing.enabled == true` |
| `l2_cache_async_lookup_enqueue_stage` | Lookup | `cache.behavior == Store` |
| `pagination_request_mutation_stage` | Lookup | `pagination.enabled == true` |
| `l1_cache_store_stage` | Store | `cache.behavior == Store` |
| `l1_variant_store_stage` | Store | `cache.behavior == Store` AND `cache.field_variant.enabled == true` |
| `l2_cache_async_store_enqueue_stage` | Store | `cache.behavior == Store` |
| `coalescing_leader_completion_stage` | Store | `coalescing.enabled == true` |
| `compression_decision_stage` | Response | Always included |

---

## Capability Combinations

### 1. No Features (Bypass Mode, Observe Mode)
* **Lookup Stages** (0): None
* **Store Stages** (0): None
* **Response Stages** (1): `compression_decision_stage`

### 2. Cache Only (Standard Storage)
* **Lookup Stages** (3):
  1. `cache_key_prepare_stage`
  2. `l1_cache_lookup_stage`
  3. `l2_cache_async_lookup_enqueue_stage`
* **Store Stages** (2):
  1. `l1_cache_store_stage`
  2. `l2_cache_async_store_enqueue_stage`
* **Response Stages** (1): `compression_decision_stage`

### 3. Cache + Coalescing
* **Lookup Stages** (5):
  1. `cache_key_prepare_stage`
  2. `l1_cache_lookup_stage`
  3. `coalescing_decision_stage`
  4. `coalescing_follower_wait_stage`
  5. `l2_cache_async_lookup_enqueue_stage`
* **Store Stages** (3):
  1. `l1_cache_store_stage`
  2. `l2_cache_async_store_enqueue_stage`
  3. `coalescing_leader_completion_stage`
* **Response Stages** (1): `compression_decision_stage`

### 4. Cache + Field Variant (Variant Caching)
* **Lookup Stages** (5):
  1. `cache_key_prepare_stage`
  2. `field_variant_admission_stage`
  3. `l1_cache_lookup_stage`
  4. `l1_variant_lookup_stage`
  5. `l2_cache_async_lookup_enqueue_stage`
* **Store Stages** (3):
  1. `l1_cache_store_stage`
  2. `l1_variant_store_stage`
  3. `l2_cache_async_store_enqueue_stage`
* **Response Stages** (1): `compression_decision_stage`

### 5. Pagination Only (Cache Disabled)
* **Lookup Stages** (1):
  1. `pagination_request_mutation_stage`
* **Store Stages** (0): None
* **Response Stages** (1): `compression_decision_stage`

### 6. Compression Only (Cache Disabled)
* **Lookup Stages** (0): None
* **Store Stages** (0): None
* **Response Stages** (1): `compression_decision_stage`

### 7. All Features Enabled
* **Lookup Stages** (8):
  1. `cache_key_prepare_stage`
  2. `field_variant_admission_stage`
  3. `l1_cache_lookup_stage`
  4. `l1_variant_lookup_stage`
  5. `coalescing_decision_stage`
  6. `coalescing_follower_wait_stage`
  7. `l2_cache_async_lookup_enqueue_stage`
  8. `pagination_request_mutation_stage`
* **Store Stages** (4):
  1. `l1_cache_store_stage`
  2. `l1_variant_store_stage`
  3. `l2_cache_async_store_enqueue_stage`
  4. `coalescing_leader_completion_stage`
* **Response Stages** (1): `compression_decision_stage`

---

## Forbidden Hot-Path Stages

Synchronous disk and database I/O are strictly forbidden inside hot-path execution stages.
- `l2_cache_lookup_stage` and `l2_cache_store_stage` must **never** be appended to `kLookupStages` or `kStoreStages` or compiled into a route's execution arrays.
- Background tasks requiring disk access must instead be enqueued to `runtime::WorkerQueue` non-blockingly.
- Build-time checks in `tests/default_pipeline_boundaries_test.cpp` enforce these architectural constraints.

---

## Enforcement Tests

- `tests/compiled_route_runtime_test.cpp`: Asserts exact stage sequences, counts, and combinations.
- `tests/default_pipeline_order_test.cpp`: Verifies execution path sequence.
- `tests/default_pipeline_boundaries_test.cpp`: Prevents forbidden sync disk I/O stages from leaking into default paths.

---

## Capacity Limits

- `kMaxCompiledRoutesCapacity = 64`: Upper limit on route runtime storage compiled at startup.
- `kMaxLookupStages = 8`: Maximum lookup stages allowed in request pipeline.
- `kMaxStoreStages = 8`: Maximum store stages allowed in response-body pipeline.
- `kMaxResponseStages = 8`: Maximum response stages allowed in response-header pipeline.

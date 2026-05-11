# ByteTaper Runtime Execution Boundaries

ByteTaper processes Envoy ExtProc gRPC streams on request threads. These threads must not perform disk I/O, network I/O, or unbounded computation. Background work runs through `runtime::WorkerQueue`, which is limited to cache I/O only.

See `include/extproc/default_pipelines.h` for the canonical stage arrays.
See `tests/default_pipeline_boundaries_test.cpp` for enforcement tests.

---

## Request-Header Pipeline (`kLookupStages`)

Runs synchronously on the gRPC request thread per request.

**Allowed:**
- Route match (pre-pipeline, `grpc_server.cpp`)
- Cache key preparation (`cache_key_prepare_stage`)
- Field variant admission (`field_variant_admission_stage`)
- L1 cache lookup (`l1_cache_lookup_stage`, `l1_variant_lookup_stage`)
- Coalescing decision (`coalescing_decision_stage`)
- Follower bounded wait (`coalescing_follower_wait_stage`) — blocks at most `backend_timeout_ms + handoff_buffer_ms`
- Async L2 lookup enqueue — non-blocking (`l2_cache_async_lookup_enqueue_stage`)
- Pagination request mutation (`pagination_request_mutation_stage`)
- Metrics counter updates

**Forbidden:**
- Synchronous L2 lookup (`l2_cache_lookup_stage`) — disk I/O
- Synchronous L2 store (`l2_cache_store_stage`) — disk I/O
- Backend calls or network I/O
- Unbounded computation or allocation
- Blocking disk cleanup or warmup

---

## Response-Header Pipeline (`kResponseStages`)

Runs synchronously on the gRPC response thread.

**Allowed:**
- Compression decision (`compression_decision_stage`)
- Response metadata capture

**Forbidden:**
- Response body parsing
- Synchronous L2 I/O
- Backend calls

---

## Response-Body Pipeline (`kStoreStages`)

Runs synchronously on the gRPC response thread per response body.

**Allowed:**
- L1 cache store (`l1_cache_store_stage`, `l1_variant_store_stage`)
- Async L2 store enqueue — non-blocking (`l2_cache_async_store_enqueue_stage`)
- Coalescing leader completion state update (`coalescing_leader_completion_stage`)
- Bounded JSON transform and field selection
- Metrics counter updates
- Fail-open / fail-closed safety evaluation

**Forbidden:**
- Synchronous L2 store (`l2_cache_store_stage`) — disk I/O
- Unbounded allocation or body accumulation
- Unbounded wait or blocking

---

## Worker Queue (`runtime::WorkerQueue`)

Background threads. Isolated from gRPC stream threads.

**Allowed:**
- RocksDB L2 lookup (`L2LookupJob`) with L1 promotion on hit
- RocksDB L2 store (`L2StoreJob`)
- Coalescing L2Ready state publication after successful store
- L2RemoveJob (future)
- L2WarmupJob (future)
- L2TtlCleanupJob (future)

**Forbidden:**
- Writing to gRPC stream or Envoy response
- Calling upstream backend
- JSON transform, compression, pagination mutation
- Running an APG pipeline (`run_pipeline`)
- General-purpose APG stage execution

---

## Shutdown / Drain

During shutdown, the worker queue drains in-flight jobs before process exit.

**Allowed:**
- Complete in-flight L2StoreJob and L2LookupJob entries already enqueued
- Mark registry entries as failed if completion is not possible

**Forbidden:**
- Accepting new enqueue requests after shutdown signal
- Blocking indefinitely on backend responses

---

## Sync L2 Stages — Test and Manual Use Only

`l2_cache_lookup_stage` and `l2_cache_store_stage` exist in the codebase for:
- Unit tests that need deterministic synchronous cache verification
- CLI / diagnostic tooling that runs outside the hot-path pipeline

These stages **must not appear** in `kLookupStages` or `kStoreStages`. The boundary tests in `tests/default_pipeline_boundaries_test.cpp` enforce this at build time.

---

## Enforcement

`tests/default_pipeline_boundaries_test.cpp` inspects `kLookupStages` and `kStoreStages` at test time and fails if any sync L2 stage (`l2_cache_lookup_stage`, `l2_cache_store_stage`) appears in either pipeline. Add a test there whenever a new stage type is introduced that could violate hot-path constraints.

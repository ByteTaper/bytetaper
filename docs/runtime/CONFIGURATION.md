# Runtime Configuration Reference

This document describes all command line arguments and environment variables supported by the ByteTaper External Processor daemon.

---

## CLI Arguments

The following flags are handled at startup by the `bytetaper-extproc-server` binary:

| Flag | Type | Required | Default | Description |
|---|---|---|---|---|
| `--listen-address` | String | No | `"0.0.0.0"` | The local address the gRPC server should listen on. |
| `--policy-file` | String | No | *(none)* | Absolute path to the YAML route policy configuration. If omitted, no static route policies are loaded at startup. |
| `--l2-cache-path` | String | No | *(none)* | Absolute path to the directory where RocksDB L2 cache files are stored. If omitted, L2 disk cache is disabled. |
| `--metrics-address` | String | No | `"0.0.0.0"` | The address the Prometheus metrics server should listen on. |
| `--metrics-port` | Int | No | `18081` | The port the Prometheus metrics server should listen on. |

---

## Environment Variables

### L2 Cache Tuning

These variables control RocksDB storage layout and background jobs.

| Variable | Default | Valid Range | Effect / Description |
|---|---|---|---|
| `BYTETAPER_L2_BLOCK_CACHE_MB` | `64` | `>= 1` | RocksDB LRU block cache size in MB. |
| `BYTETAPER_L2_WRITE_BUFFER_MB` | `32` | `>= 1` | RocksDB memtable write buffer size in MB. |
| `BYTETAPER_L2_MAX_BACKGROUND_JOBS` | `2` | `>= 1` | RocksDB background compaction and flush threads. |

> [!NOTE]
> **Memory Usage Note:**  
> Minimum RocksDB RSS footprint is roughly calculated as:  
> `BYTETAPER_L2_BLOCK_CACHE_MB + (BYTETAPER_L2_WRITE_BUFFER_MB × max_write_buffer_number)`

---

### Worker Queue Sizing

These variables tune the background concurrency and execution slots of `runtime::WorkerQueue`.

| Variable | Default | Valid Range | Effect / Description |
|---|---|---|---|
| `BYTETAPER_WORKER_COUNT` | `2` | `1..8` | Background worker threads (max: `kWorkerQueueMaxWorkers`). |
| `BYTETAPER_LOOKUP_LANE_QUOTA` | `4` | `>= 1` | L2 lookup jobs drained per worker thread cycle. |
| `BYTETAPER_STORE_LANE_QUOTA` | `1` | `>= 1` | L2 store jobs drained per worker thread cycle. |
| `BYTETAPER_ASYNC_STORE_MAX_BODY_SIZE` | *(derived)* | `>= 1` | Upper limit on response body size enqueued asynchronously to L2. **Note: A value of `0` is invalid and will cause parsing rejection; if you need default behavior, leave this environment variable unset.** |

> [!IMPORTANT]
> **Deriving ASYNC_STORE_MAX_BODY_SIZE:**  
> If `BYTETAPER_ASYNC_STORE_MAX_BODY_SIZE` is absent, the value is derived dynamically based on the loaded route policies:
> - The server scans all active routes' `max_response_bytes` configurations.
> - The effective cap becomes the maximum configured `max_response_bytes` across routes, capped at `kAsyncL2StoreAbsoluteMaxBodySize` (1 MiB / 1048576 bytes).
> - If no route has a size limit, or if there is an unlimited policy (value of `0`), the cap falls back to the default of `65536` bytes (64 KiB).

> [!TIP]
> **Lane Quota Optimization:**
> - **Higher `LOOKUP_LANE_QUOTA`**: Focuses on draining lookup jobs first. Improves L2 cache-hit latency under reading bursts, but might cause temporary store queue starvation.
> - **Higher `STORE_LANE_QUOTA`**: Drains store jobs faster, which speeds up coalescing follower wakeups (L2Ready signals), but can increase read queue latency.
> - Diagnostic metrics to watch: `bytetaper_worker_store_lane_starvation_total` and `bytetaper_worker_lookup_lane_wait_ms_avg`.

---

### Hash Hardening

| Variable | Default | Valid Range | Effect / Description |
|---|---|---|---|
| `BYTETAPER_HASH_SEED_HEX` | *(random)* | 32 hex chars | Cache key hash seed. |

> [!IMPORTANT]
> If absent, a random hash seed is generated at startup (strongly recommended for production to prevent hash collision attacks). Setting an explicit Hex seed is useful for debugging, test reproducibility, and manual key checking. Never log or leak this seed in metrics or trace files.

---

### Tracing

| Variable | Default | Valid Values | Effect / Description |
|---|---|---|---|
| `BYTETAPER_TRACE_ENABLED` | `false` | `"true"`, `"false"` | Globally enable or disable the request tracing subsystem. |
| `BYTETAPER_TRACE_MODE` | `"slow"` | `"off"`, `"slow"`, `"sampled"`, `"all"` | Controls tracing trigger behavior. |
| `BYTETAPER_TRACE_OUTPUT_DIR` | `"reports/traces"` | Directory path | File location where JSON trace documents are written. Traces are not persisted if empty. |
| `BYTETAPER_TRACE_SLOW_TOTAL_MS` | `30` | Int milliseconds | Trace requests that exceed N milliseconds of total latency. |
| `BYTETAPER_TRACE_SLOW_PHASE_MS` | `10` | Int milliseconds | Trace requests where any phase duration exceeds N milliseconds. |
| `BYTETAPER_TRACE_SAMPLE_RATE` | `0.01` (1%) | `0.0..1.0` (Float) | Fraction of requests traced when sampling triggers are active (expressed internally in PPM; 1% sample rate is 10000 PPM). |
| `BYTETAPER_TRACE_MAX_RECORDS` | `4096` | `1..4096` | Upper limit on trace record count captured in-memory (hard cap is 4096). |
| `BYTETAPER_TRACE_SCENARIO` | *(none)* | String | Identifier string added to trace reports to group/identify runs. |

> [!NOTE]
> **Backward Compatibility Aliases:**  
> The following deprecated aliases are still accepted as fallbacks:
> - `BYTETAPER_TRACE_SLOW_MS` → `BYTETAPER_TRACE_SLOW_TOTAL_MS`
> - `BYTETAPER_TRACE_PHASE_SLOW_MS` → `BYTETAPER_TRACE_SLOW_PHASE_MS`

---

## Tuning Profiles

### 1. High Read Throughput (Read-Heavy Cache API)
Highly tuned for draining read lookups from RocksDB rapidly.
```bash
BYTETAPER_WORKER_COUNT=4
BYTETAPER_LOOKUP_LANE_QUOTA=8
BYTETAPER_STORE_LANE_QUOTA=1
BYTETAPER_L2_BLOCK_CACHE_MB=256
```

### 2. High Coalescing Store (Write-Heavy Coalescing Handoff)
Optimized for high-concurrency write surges and rapid follower wakeup notification.
```bash
BYTETAPER_WORKER_COUNT=4
BYTETAPER_LOOKUP_LANE_QUOTA=2
BYTETAPER_STORE_LANE_QUOTA=4
BYTETAPER_L2_WRITE_BUFFER_MB=64
BYTETAPER_L2_MAX_BACKGROUND_JOBS=4
```

### 3. Memory-Constrained Environment
Minimizes standard buffer sizes to ensure ultra-low RSS profile in memory-capped environments.
```bash
BYTETAPER_L2_BLOCK_CACHE_MB=16
BYTETAPER_L2_WRITE_BUFFER_MB=8
BYTETAPER_WORKER_COUNT=1
BYTETAPER_ASYNC_STORE_MAX_BODY_SIZE=32768
```

---

## Config Metrics

The effective startup configuration values are exposed as Prometheus gauges through the `/metrics` endpoint. This allows operators, benchmarks, and CI suites to scrape and verify the actual configuration applied at runtime.

The following gauge metrics are available:

| Metric Name | Type | Description |
|---|---|---|
| `bytetaper_worker_count_effective` | Gauge | Effective number of background worker threads. |
| `bytetaper_worker_lookup_lane_quota_effective` | Gauge | Effective lookup lane quota. |
| `bytetaper_worker_store_lane_quota_effective` | Gauge | Effective store lane quota. |
| `bytetaper_worker_async_store_max_body_size_effective` | Gauge | Effective async store max body size in bytes (0 = unset/not started). |
| `bytetaper_l2_block_cache_mb_effective` | Gauge | Effective L2 RocksDB block cache size in MiB (0 = L2 not configured). |
| `bytetaper_l2_write_buffer_mb_effective` | Gauge | Effective L2 RocksDB write buffer size in MiB (0 = L2 not configured). |
| `bytetaper_l2_max_background_jobs_effective` | Gauge | Effective L2 RocksDB max background compaction jobs (0 = L2 not configured). |

> [!NOTE]
> **Secret Protection:**  
> The cache key hash seed (`BYTETAPER_HASH_SEED_HEX`) is sensitive and is **never** exposed through the metrics endpoint or log files.

# L2 Cache Backend Configuration & Policy

ByteTaper's Tiered Caching framework leverages an **L2 Disk Cache** backend powered by **RocksDB**. This layer provides high-throughput, persistent storage that retains cache entries across daemon restarts, reducing origin server load and eliminating redundant payload transformations over long durations.

---

## 1. Core Architectural Philosophy

When designing configuration access to the RocksDB storage engine, ByteTaper maintains a strict separation of concerns:

> [!IMPORTANT]
> **Cache Semantics vs. Resource Sizing**
> * **Cache Semantics** are absolute rules required to preserve system correctness, performance, and memory boundaries. These are strictly enforced in code and **cannot** be altered by deployment operators.
> * **Resource Sizing** variables are capacity-oriented constraints that depend on host-level environments (such as available RAM and CPU cores). These are exposed as **operator-configurable knobs**.

This division ensures that operators can safely tune ByteTaper's memory and CPU usage to their specific hardware without risking silent cache corruption, resource starvation, or a breakdown of cache consistency.

---

## 2. Operator-Configurable Resource Sizing

Operators can tune the global memory and thread limits of the RocksDB storage engine by setting the following system environment variables on the gRPC external processor container:

| Environment Variable | Target Field | Default Value | Tuning Impact | Why Expose |
| :--- | :--- | :--- | :--- | :--- |
| `BYTETAPER_L2_BLOCK_CACHE_MB` | `block_cache_mb` | `64` | Sizes RocksDB's in-memory LRU read cache. | **Primary Memory Knob**: Deployment environments have vastly different RAM budgets. Higher values increase L2 cache-hit latency efficiency at the cost of container memory. |
| `BYTETAPER_L2_WRITE_BUFFER_MB` | `write_buffer_mb` | `32` | Sizes the active in-memory Write Memtable. | **Write-Throughput Control**: Influences peak memory and write write-back pacing. Legitimate tuning for write-heavy caching patterns vs. read-heavy setups. |
| `BYTETAPER_L2_MAX_BACKGROUND_JOBS` | `max_background_jobs` | `2` | Controls concurrency for compaction and flushes. | **I/O Parallelism Control**: Capping this parameter protects disk I/O channels when co-locating ByteTaper on resource-constrained hosts or shared VM storage. |

---

## 3. Internal Cache-Semantic Policies

To prevent deployment misconfigurations, critical database engine behaviors are hardcoded internally within `l2_open_with_options` and are **not yet** exposed to operators.

### Why Core Parameters are Enforced Internally

#### 1. Enforcing Cache Durability (`disable_wal = true`)
* **Rationale**: The write-ahead log (WAL) guarantees transaction durability across database crashes. However, ByteTaper’s L2 is a **throwaway transaction-safe cache**, not a source of truth. Disabling the WAL eliminates unnecessary disk I/O overhead on writes and ensures rapid cache population.
* **Why Hidden**: Exposing a WAL configuration could mislead operators into treating the L2 cache as a durable datastore. If a crash occurs, L2 simply falls back to origin fetches, maintaining robust system semantics without sacrificing performance.

#### 2. Self-Provisioning Storage (`create_if_missing = true`)
* **Rationale**: This is required to initialize the database folder cleanly upon first boot.
* **Why Hidden**: There is no valid use case for setting this to `false`. Exposing it only introduces a way for operators to break startup on dynamic or containerized node environments.

#### 3. Optimized Block Indexes (`cache_index_and_filter_blocks = true`)
* **Rationale**: Pinning index and Bloom filter blocks inside our global memory block cache bounds RocksDB's off-heap memory consumption.
* **Why Hidden**: If left unpinned, index tables can grow unboundedly outside the specified block cache boundaries, causing unpredictable container OOM termination. Operators cannot safely reason about this without deep internal knowledge of RocksDB's memory layout.

#### 4. Fixed Write Buffer Count (`max_write_buffer_number = 2`)
* **Rationale**: Limits the peak number of memtables to avoid volatile memory consumption spikes.
* **Why Hidden**: Setting this value too high in conjunction with a large `write_buffer_mb` can cause exponential memory usage spikes during high-throughput burst states.

#### 5. Silent Compression Handshakes (`enable_compression = true`)
* **Rationale**: Automatically defaults to fast compression (`kLZ4Compression`) to minimize disk footprint and optimize read-path disk I/O.
* **Why Hidden**: LZ4 is universally optimal for web payload cache data, offering high throughput with minimal CPU overhead. Standardizing this behavior simplifies the operational landscape and keeps deployment configurations clean.

---

## 4. Example Deployment Overrides

To adjust the global L2 cache parameters for high-throughput or low-memory environments, operators simply define the resource overrides in their orchestration configs:

```yaml
version: "3.8"

services:
  bytetaper-extproc:
    image: bytetaper-dev:latest
    environment:
      # Sizing for a low-memory, compact environment
      - BYTETAPER_L2_BLOCK_CACHE_MB=32
      - BYTETAPER_L2_WRITE_BUFFER_MB=16
      - BYTETAPER_L2_MAX_BACKGROUND_JOBS=1
    volumes:
      - /var/cache/bytetaper:/tmp/bytetaper-cache
    command:
      - ./build/bytetaper-extproc-server
      - --listen-address
      - 0.0.0.0:18080
      - --l2-cache-path
      - /tmp/bytetaper-cache
```

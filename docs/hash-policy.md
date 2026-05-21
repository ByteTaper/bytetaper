# Hash Hardening & Shard Selection Policy

This document defines the architecture, rationale, configuration, and developer guidelines for the centralized hash hardening mechanism implemented in ByteTaper.

---

## 1. Overview & Architectural Goals

ByteTaper operates as an edge API gateway and high-throughput proxy where request parameters, query strings, header values, and cache keys are directly influenced by untrusted external traffic. 

To achieve $O(1)$ efficiency, key components shard internal state using hash functions. Legacy implementations utilized predictable, non-keyed hashes:
* **DJB2** in the L1 Cache and Runtime Worker Queue.
* **FNV-1a** in the Coalescing In-Flight Registry.

Because non-keyed hashes are deterministic and globally predictable, an adversary could easily craft a sequence of keys that collide under standard DJB2 or FNV-1a. This leads to **hash-flooding attacks**, where traffic is concentrated onto a single internal shard, causing extreme lock contention, queue exhaustion, high p99/p999 latency, and potential Denial of Service (DoS).

To eliminate this vulnerability, ByteTaper introduces a centralized, cryptographically robust **SipHash-2-4** based hash utility to harden internal sharding against predictable hotspots.

---

## 2. Keyed Hashing via SipHash-2-4

### Why SipHash-2-4?
SipHash-2-4 is a cryptographically strong pseudorandom function (PRF) specifically optimized for high speed on short inputs (e.g., typical URL paths, header keys, or query parameters). 
* **Keyed PRF**: SipHash requires a 128-bit key (seed). Without knowing the seed, an attacker cannot predict the hash values of arbitrary inputs or construct collisions.
* **Security**: It is specifically designed to resist hash-flooding DoS patterns.
* **Performance**: Highly efficient 64-bit word operations requiring no heap allocation, no exceptions, and zero dynamic memory overhead.

### Safe Centralized API
The hash interface is encapsulated under the `bytetaper::hash` namespace in `include/hash/hash.h`:

```cpp
namespace bytetaper::hash {

struct HashSeed {
    std::uint64_t k0 = 0;
    std::uint64_t k1 = 0;
};

// Initializes the process-wide random seed. Called at startup.
void init_process_hash_seed();

// Re-initializes/forces a specific seed for deterministic testing.
void set_process_hash_seed_for_test(HashSeed seed);

// Resets/re-rolls the random process seed.
void reset_process_hash_seed_for_test();

// Retrieves the active process seed.
HashSeed process_hash_seed();

// Raw-byte SipHash-2-4 implementation.
std::uint64_t siphash24_bytes(const char* data, std::size_t len, HashSeed seed);

// Hash helper for null-terminated strings using a specific seed.
std::uint64_t hash_cstr_keyed(const char* str, HashSeed seed);

// Default runtime hash using the globally initialized process seed.
std::uint64_t hash_cstr_runtime(const char* str);

} // namespace bytetaper::hash
```

---

## 3. Seed Lifetime & Entropy Hierarchy

The process seed (`HashSeed`) is stable for the lifetime of the process and must not be regenerated per request. The seed is initialized using a strict hierarchy of entropy sources during startup (`init_process_hash_seed()`):

1. **Deterministic Hex Override**: If the environment variable `BYTETAPER_HASH_SEED_HEX` is set to exactly 32 valid hexadecimal characters, it is parsed into `k0` and `k1`. This allows reproducible debugging and benchmarking.
2. **Hardware Entropy**: If no hex override is set, the system attempts to gather high-quality entropy from `std::random_device`.
3. **Fallback Mixing**: If `std::random_device` is unavailable or non-functional, the system mixes standard system time (high-precision clock), process identifier (`pid`), and memory addresses of local functions as an internal state entropy seed.

To prevent credential leakage, the active random seed is never exposed in normal diagnostic logs or API responses.

---

## 4. Component-Level Integrations

The hardened keyed hash function is integrated directly into three critical hot-path sharding layers:

### A. L1 Cache Sharding
* **File**: `src/cache/l1_cache.cpp`
* **Mechanism**: Selecting the target cache bucket uses the process-keyed hash:
  ```cpp
  const std::uint64_t h = bytetaper::hash::hash_cstr_runtime(key);
  const std::size_t shard_idx = h % kL1ShardCount;
  ```
* **Correctness Guarantees**: Fast slot-level pre-filtering continues to use the 64-bit hash. Because hashes are process-lifetime stable, a key always maps to the same shard. Under lock-free conditions, final string comparisons (`std::strcmp`) remain fully active to protect against accidental hash collisions.

### B. Coalescing In-Flight Registry
* **File**: `src/coalescing/inflight_registry.cpp`
* **Mechanism**: Request coalescing routes the active leader/follower registration state to a specific registry shard using the centralized process-wide hash:
  ```cpp
  const std::uint64_t h = bytetaper::hash::hash_cstr_runtime(key);
  const std::size_t shard_idx = h % kInFlightRegistryShardCount;
  ```
* **Performance Impact**: Replacing standard FNV-1a with SipHash-2-4 introduces negligible CPU cycles while completely neutralizing the threat of a targeted request queue pile-up on a single coalescing shard.

### C. Runtime Worker Queue
* **File**: `src/runtime/worker_queue.cpp`
* **Mechanism**: Distributes L2 load/store operations and pending lookups across concurrent worker queues.
* **Narrowing Helper**: The queue stores a compact `std::uint32_t` key hash for lookup deduplication. High-entropy narrowing is performed using XOR folding:
  ```cpp
  const std::uint64_t h64 = bytetaper::hash::hash_cstr_runtime(job.key);
  const std::uint32_t h32 = static_cast<std::uint32_t>(h64 ^ (h64 >> 32));
  ```
  The full `h64` is utilized for worker queue selection via `h64 % kRuntimeShardCount`.

---

## 5. Configuration & Environment Variables

These environment variables configure and manage process-level hash sharding behaviors:

| Variable | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `BYTETAPER_HASH_SEED_HEX` | `string` (32 hex characters) | *(empty)* | Forces a deterministic 128-bit seed. Used for CI testing, benchmarking, and multi-node correlation. Example: `00112233445566778899aabbccddeeff` |

---

## 6. Developer Guidelines & Compliance

### Orthodox C++ Constraints
The hash library adheres strictly to ByteTaper's Orthodox C++ principles:
* No dynamic allocation (`new` or `std::vector`) inside the hashing hot paths.
* Zero exception throwing (`noexcept`).
* Zero external RTTI or heavy dependency requirements.

### Key Null Safety
The raw byte-oriented interface provides robust safety boundaries:
```cpp
// If data is nullptr, siphash24_bytes safely treats len as 0 and does not dereference.
std::uint64_t siphash24_bytes(nullptr, non_zero_len, seed); // Safe, returns empty hash.
```
* Passing `nullptr` as a string parameter to `hash_cstr_keyed` or `hash_cstr_runtime` behaves consistently, hashing the empty byte sequence safely.

### Test Reproducibility
To ensure unit and integration tests do not become flaky due to a randomized process seed, tests that rely on shard indexing must configure a deterministic seed:
```cpp
class MyComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Enforce stable sharding across test runs
        bytetaper::hash::set_process_hash_seed_for_test({ 0x12345678ULL, 0x87654321ULL });
    }
    void TearDown() override {
        bytetaper::hash::reset_process_hash_seed_for_test();
    }
};
```
These APIs ensure that CI test pipelines remain deterministic, stable, and completely reproducible.

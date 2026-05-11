# Body Size Contract

ByteTaper classifies response body sizes into five tiers at response time.
The tier determines cache behavior, L2 storage eligibility, and coalescing
follower handoff eligibility.

## Constants

| Constant | Value | Location |
|---|---|---|
| `cache::kL1MaxBodySize` | 3072 bytes (3 KiB) | `include/cache/l1_cache.h` |
| `apg::ApgTransformContext::kL2BodyBufSize` | 65536 bytes (64 KiB) | `include/apg/context.h` |
| `cache::kL2MaxBodySize` | 1048576 bytes (1 MiB) | `include/cache/l2_disk_cache.h` |

## Tier Table

| Tier | Body size | L1 | L2 store | Follower handoff |
|---|---|---|---|---|
| Not cacheable | `0` | — | — | — |
| L1 inline | `<= kL1MaxBodySize` (3 KiB) | stored inline | async enqueue | L1 hit |
| L2 completion | `> kL1MaxBodySize && <= kL2BodyBufSize` (64 KiB) | — | async enqueue, waits for worker | follower sync L2 probe |
| L2 warm-only | `> kL2BodyBufSize && <= kL2MaxBodySize` (1 MiB) | — | async enqueue | none — too large for context buffer |
| Too large | `> kL2MaxBodySize` | — | skipped | none |

## L2-Cacheable ≠ Follower-Handoff-Eligible

A response body can be stored in L2 (the RocksDB disk cache) without being
deliverable to a coalescing follower in the same request cycle.

L2 lookup inside the follower wait path reads the response body into
`ApgTransformContext::l2_body_buf`, which is a fixed-size buffer of
`kL2BodyBufSize` (64 KiB). A body larger than this buffer fits in L2 but
cannot be returned through the follower sync probe path.

**L2 warm-only bodies** (tier 4) are stored for future cache hits by independent
requests. They do not trigger an immediate follower handoff.

## Classification Function

`decide_coalescing_completion_handoff()` in
`src/coalescing/coalescing_completion_handoff.cpp` is the canonical classifier
for coalescing completion and handoff eligibility. Note that other stages (such
as L2 async store enqueue) may perform additional, stage-specific body-size checks
to gate disk storage.

## Enforcement

Tests that verify each tier:

- `tests/coalescing_leader_completion_test.cpp` — `HandoffClassificationTiers` test
  asserts exact classification for boundary values at each tier.
- `tests/coalescing_follower_wait_test.cpp` — follower behavior for L2 completion
  tier (`FollowerDirectWait_L2Ready`) and L2 warm-only fallback
  (`FollowerTooLargeForHandoffTransition`).
- `tests/l2_cache_async_store_enqueue_stage_test.cpp` — store enqueue gating by tier.

See also: [Runtime Execution Boundaries](RUNTIME_BOUNDARIES.md),
[Compiled Route Runtime](COMPILED_ROUTE_RUNTIME.md).

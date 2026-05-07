# ⏱️ ByteTaper Request Coalescing Telemetry Integration Walkthrough

This document records the design, implementation, and successful validation of the request coalescing tail telemetry system (BT-037G). The goal of this task was to integrate end-to-end trace semantics into the `ApgTransformContext` and compile/test pipelines, enabling granular observability into why suppression succeeds or fails without introducing stdout logging or dynamic allocations.

---

## 🛠️ Implementation Summary

### 1. Unified Telemetry Model (`include/observability/trace.h`)
We extended `TraceRecord` with a dedicated coalescing context containing fixed-size character arrays to hold critical diagnostic details:
- `coalescing_group_id` (string: e.g., `group-2577600886`)
- `coalescing_key_hash` (string: 64-bit Hex)
- `coalescing_role` (enum represented as string: `"leader"` | `"follower"` | `"bypass"`)
- `coalescing_decision` (string: `"leader_fill"`, `"follower_wait"`, `"follower_consume_result"`, etc.)
- `coalescing_attach_result` (string: `"success"` | `"failed"` | `"not_applicable"`)
- `coalescing_attach_failure_reason` (string: `"none"` | `"shard_full"` | `"max_waiters_exceeded"`)
- `coalescing_wakeup_reason` (string: `"result_ready"` | `"timeout"` | `"cancelled"`)
- `coalescing_result_source` (string: `"upstream"` | `"coalesced_result"`)
- `coalescing_upstream_call_reason` (string: `"leader_fill"` | `"fallback"`)
- `coalescing_has_context` (gate boolean: to selectively emit these fields only when coalescing applies)

We also added trace span name string constants (e.g. `kSpanCoalescingKeyBuild`, `kSpanCoalescingInflightLookup`, etc.) to standardize trace spans across different stages.

### 2. Zero-Allocation Serializer (`src/observability/trace.cpp`)
We updated `trace_format_jsonl` to parse and serialize the newly introduced coalescing block directly from the static fields when `record.coalescing_has_context` is `true`. The formatting buffer size was increased to `1024` bytes to prevent compiler truncation warnings.

Additionally, `trace_flush` was extended to aggregate the active trace record array by `coalescing_group_id` before exporting:
- It automatically evaluates the success metric of each cohort.
- Generates a markdown report (`<ts>.trace_summary.md`) detailing client request distribution, leader/follower alignment, served from results, timeout fallbacks, and unexpected upstream counts.
- Enforces a high-integrity contract: groups where `upstream_calls <= leaders` and `timeout_fallbacks == 0` receive a **PASS** verdict.

### 3. Pipeline gRPC Wiring (`src/extproc/grpc_server.cpp` and `include/apg/context.h`)
- Added a non-owning pointer `observability::TraceRecord* trace` to `ApgTransformContext`.
- Added high-fidelity tracking strings to standard stages within the context.
- In `grpc_server.cpp`, we assigned the active trace block: `filter_state.context.trace = &filter_state.trace`.
- Post-pipeline completion, we assigned tracking strings to the active trace record using `trace_set_coalescing_context`.

### 4. Stage Instrumentation
We wrapped critical sections in the coalescing stages inside nested or sibling trace scopes (`TraceSpanScope`):
- **`coalescing_decision_stage.cpp`**: Spans for `coalescing.key.build`, `coalescing.inflight.lookup`, and `coalescing.role.decide`.
- **`coalescing_follower_wait_stage.cpp`**: Spans for `coalescing.follower.attach` (`ActiveProcessingDetail`), `coalescing.follower.wait` (`RuntimeQueueWait`), and outcome spans (`coalescing.follower.consume_result` or `coalescing.follower.timeout`).
- **`coalescing_leader_completion_stage.cpp`**: Spans for `coalescing.leader.publish_result` and `coalescing.leader.notify_followers`.

---

## 🚦 Verification and Quality Assurance

### 1. Build Verification
To verify Orthodox C++ compliance and prevent linker failures, we updated the CMake configuration:
- Option `-DBYTETAPER_ENABLE_GTEST_TESTS=ON` was successfully used inside Docker.
- Added library dependency `bytetaper_trace` to `coalescing_decision_test`, `coalescing_timeout_test`, and `coalescing_leader_completion_test`.
- Compilation completed cleanly with **zero warnings and zero compilation errors**.

### 2. Concurrency-Safe Unit & Integration Tests
We executed the entire regression suite within the standard isolated environment:
```bash
docker run --rm -v $(pwd):/workspace -w /workspace/build bytetaper-dev:latest ctest --output-on-failure
```
**Result**: **119/119 tests passed successfully (100% success rate)**.

### 3. End-to-End Concurrency Validation (Load Burst Scenarios)
We evaluated the full pipeline under extreme request coalescing pressure (50 parallel client requests) using the load scenario runner:
```bash
BYTETAPER_TRACE_ENABLED=true \
BYTETAPER_TRACE_MODE=all \
BYTETAPER_TRACE_OUTPUT_DIR=reports/traces \
docker-compose up --build bytetaper-coalescing-burst-test
```

The system delivered excellent suppression ratios and produced high-fidelity group summaries:
- **Phase 1 (Fast Paths)**: Suppressed 5 parallel client requests to exactly **1 upstream call** (reduction ratio: 1/5).
- **Phase 2 (Slow Paths with Wait Window)**: Suppressed 5 concurrent slow client requests to exactly **1 upstream call** with **zero fallbacks or timeouts**.

---

## 📊 Sample Aggregated Group Summary (`default_20260507_115834.trace_summary.md`)

```markdown
# 🧪 ByteTaper Coalescing Group-level Trace Summary

| Group ID | Client Requests | Leaders | Followers | Followers Attached | Served from Result | Followers Timeout | Fallbacks | Upstream Calls | Unknown Upstreams | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `group-2577600886` | 5 | 1 | 4 | 5 | 4 | 0 | 0 | 1 | 0 | **PASS** |
| `group-1017122064` | 5 | 1 | 4 | 5 | 4 | 0 | 0 | 1 | 0 | **PASS** |

## 📝 Detailed Analysis

### Group `group-2577600886`
- **Verdict**: PASS
- **Suppression efficiency**: All client requests were satisfied with at most 1 upstream call per leader and zero fallback timeout failures.

### Group `group-1017122064`
- **Verdict**: PASS
- **Suppression efficiency**: All client requests were satisfied with at most 1 upstream call per leader and zero fallback timeout failures.
```

The generated `.summary.md` and `.trace.jsonl` files are stored under [reports/traces/](file:///Users/haluan.irsad/Documents/go-work/code/bytetaper/reports/traces).

# ByteTaper Benchmarks

This directory contains scripts and configuration for reproducible performance benchmarking of ByteTaper.

## 🚀 Usage

Benchmarks are executed inside a dedicated Docker container to ensure consistent results across different environments. No local benchmarking tools are required.

### Run Default Benchmark Scenario

```bash
docker compose run --rm bytetaper-benchmark
```

By default, this executes the standard observe mode benchmark.

### Run Custom Benchmark Scenarios

You can run individual scenario scripts directly inside the container to test isolated features:

```bash
# Run Performance Smoke Test (3s CI gate)
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/performance_smoke.sh

# Run Envoy-Only Baseline
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/envoy_only.sh

# Run Observe Mode
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/bytetaper_observe.sh

# Run JSON Field Filtering
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/bytetaper_field_filtering.sh

# Run Cache Promotion
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/cache_hit.sh

# Run Pagination Guardrail
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/pagination_guardrail.sh

# Run Compression Coordination
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/compression_coordination.sh

# Run Request Coalescing
docker compose run --rm bytetaper-benchmark ./benchmarks/scenarios/coalescing_burst.sh

# Run with trace enabled
## 1. Run the benchmark with trace
BYTETAPER_TRACE_ENABLED=true \
BYTETAPER_TRACE_SCENARIO="compression_coordination" \
BYTETAPER_TRACE_SLOW_MS=30 \
BYTETAPER_TRACE_OUTPUT_DIR="reports/traces" \
docker-compose run --rm -e CMAKE_BUILD_PARALLEL_LEVEL=1 bytetaper-benchmark ./benchmarks/scenarios/compression_coordination.sh

## 2. Generate report need to tear down
docker-compose down
```

---

## 📊 Reports Compilation

Every benchmark run produces three outputs under the `reports/benchmarks/` directory:
1. **Raw Text Report (`*.txt`)**: Human-readable wrk telemetry log.
2. **Structured JSON Report (`*.json`)**: Machine-readable data structure representing latencies, throughput, container stats, and payload savings.
3. **Executive Markdown Report (`*.md`)**: High-fidelity formatted summary with tables and warning flags.

At the end of each run, the validation compiler evaluates metrics against defined limits in [performance-thresholds.yaml](file:///Users/haluan.irsad/Documents/go-work/code/bytetaper/benchmarks/performance-thresholds.yaml), exiting non-zero if a regression is detected.

---

## 📖 Documentation

For a comprehensive guide on interpreting latency percentiles, payload calculations, container stats limits, and attaching reports to readiness reviews, see the [Production Benchmark Guide](file:///Users/haluan.irsad/Documents/go-work/code/bytetaper/docs/production/performance-benchmark-guide.md).


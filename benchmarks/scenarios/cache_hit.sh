#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

SCENARIO="cache_hit"
TARGET_HOST="${ENVOY_CACHE_URL:-http://envoy-bytetaper-cache-a:10000}"
METRICS_HOST="${METRICS_CACHE_URL:-http://bytetaper-extproc-cache-a:18081/metrics}"
MOCK_API="${MOCK_API_URL:-http://mock-api:8080}"
REPORT_DIR="reports/benchmarks"
CHECKPOINT_FILE="${REPORT_DIR}/cache_hit_checkpoint.json"

source ./benchmarks/lib/mock_api_metrics.sh

mkdir -p "$REPORT_DIR"

echo "Checking targets availability..."
if ! curl -s --fail "${TARGET_HOST}/products/health" > /dev/null; then
    echo "ERROR: Target Envoy ${TARGET_HOST} is unavailable."
    exit 1
fi
if ! curl -s --fail "${MOCK_API}/metrics.json" > /dev/null; then
    echo "ERROR: Mock API metrics ${MOCK_API}/metrics.json is unavailable."
    exit 1
fi

echo "Targets are UP."

if [ ! -f "$CHECKPOINT_FILE" ]; then
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    echo "--------------------------------------------------"
    echo "Starting Phase 1 (Miss & L1 Hit profiles)"
    echo "--------------------------------------------------"

    # --------------------------------------------------
    # Section 1: Cache Miss
    # --------------------------------------------------
    echo "Section 1: Cache Miss Profile"

    mock_api_reset_metrics "$MOCK_API"
    curl -sD /tmp/miss_headers.txt "${TARGET_HOST}/products/miss_test_${TIMESTAMP}" > /dev/null
    miss_probe_metrics_json=$(mock_api_snapshot "$MOCK_API")
    miss_probe_calls=$(mock_api_upstream_requests "$miss_probe_metrics_json")

    cached_header=$(grep -i 'x-bytetaper-cached-response' /tmp/miss_headers.txt | tr -d '\r' | awk '{print $2}' || echo "false")
    layer_header=$(grep -i 'x-bytetaper-cache-layer' /tmp/miss_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")

    echo "Single Miss Request Cached-Header: ${cached_header}"
    echo "Single Miss Request Cache-Layer: ${layer_header}"
    echo "Upstream Call Delta for Miss Probe: ${miss_probe_calls}"
    echo ""

    if [ "${miss_probe_calls}" -lt 1 ]; then
        echo "ERROR: Expected at least 1 upstream call for cache miss, got ${miss_probe_calls}!"
        exit 1
    fi

    echo "Running wrk load test on Cache Miss (first request populates, subsequent requests should reuse cache)..."
    mock_api_reset_metrics "$MOCK_API"
    WRK_MISS_OUT=$(mktemp)
    wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "${TARGET_HOST}/products/miss_bench_${TIMESTAMP}" | tee "$WRK_MISS_OUT"
    miss_metrics_json=$(mock_api_snapshot "$MOCK_API")
    miss_calls=$(mock_api_upstream_requests "$miss_metrics_json")

    echo "Extracting Miss latency JSON..."
    miss_latency_json=$(./benchmarks/lib/latency_parser.sh "$WRK_MISS_OUT")
    echo "Extracting Miss throughput JSON..."
    miss_throughput_json=$(./benchmarks/lib/throughput_parser.sh "$WRK_MISS_OUT")

    # --------------------------------------------------
    # Section 2: L1 Cache Hit
    # --------------------------------------------------
    echo ""
    echo "Section 2: L1 Cache Hit Profile"

    L1_KEY="l1_hit_${TIMESTAMP}"

    echo "Warming up L1 Cache..."
    curl -s "${TARGET_HOST}/products/${L1_KEY}" > /dev/null

    mock_api_reset_metrics "$MOCK_API"
    curl -sD /tmp/l1_headers.txt "${TARGET_HOST}/products/${L1_KEY}" > /dev/null
    l1_probe_metrics_json=$(mock_api_snapshot "$MOCK_API")
    l1_probe_calls=$(mock_api_upstream_requests "$l1_probe_metrics_json")

    l1_cached=$(grep -i 'x-bytetaper-cached-response' /tmp/l1_headers.txt | tr -d '\r' | awk '{print $2}' || echo "false")
    l1_layer=$(grep -i 'x-bytetaper-cache-layer' /tmp/l1_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")

    echo "L1 Hit Request Cached-Header: ${l1_cached}"
    echo "L1 Hit Request Cache-Layer: ${l1_layer}"
    echo "Upstream Call Delta for L1 Hit Probe: ${l1_probe_calls}"
    echo ""

    if [ "${l1_cached}" != "true" ] || [ "${l1_layer}" != "L1" ]; then
        echo "ERROR: L1 cache hit headers are incorrect!"
        exit 1
    fi
    if [ "${l1_probe_calls}" -ne 0 ]; then
        echo "ERROR: L1 cache hit triggered an upstream call!"
        exit 1
    fi

    echo "Running wrk load test on warm L1 Cache..."
    mock_api_reset_metrics "$MOCK_API"
    WRK_L1_OUT=$(mktemp)
    wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "${TARGET_HOST}/products/${L1_KEY}" | tee "$WRK_L1_OUT"
    l1_metrics_json=$(mock_api_snapshot "$MOCK_API")
    l1_calls=$(mock_api_upstream_requests "$l1_metrics_json")

    if [ "${l1_calls}" -ne 0 ]; then
        echo "ERROR: Warm L1 load test triggered upstream calls: ${l1_calls}"
        exit 1
    fi

    echo "Extracting L1 Hit latency JSON..."
    l1_latency_json=$(./benchmarks/lib/latency_parser.sh "$WRK_L1_OUT")
    echo "Extracting L1 Hit throughput JSON..."
    l1_throughput_json=$(./benchmarks/lib/throughput_parser.sh "$WRK_L1_OUT")

    # --------------------------------------------------
    # Warm up L2 (Populate entry that survives restart)
    # --------------------------------------------------
    L2_KEY="l2_hit_${TIMESTAMP}"
    echo ""
    echo "Warming up L2 with key ${L2_KEY}..."
    curl -s "${TARGET_HOST}/products/${L2_KEY}" > /dev/null

    echo "Waiting 2 seconds to let asynchronous background threads persist entry to disk..."
    sleep 2

    miss_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_MISS_OUT" | awk '{print $1}' || echo "0")
    l1_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_L1_OUT" | awk '{print $1}' || echo "0")

    echo "Extracting container stats for Miss..."
    miss_stats_json=$(./benchmarks/lib/container_stats.sh all)
    echo "Extracting container stats for L1 Hit..."
    l1_stats_json=$(./benchmarks/lib/container_stats.sh all)

    echo "Extracting payload savings for Miss..."
    miss_size=$(curl -s -o /dev/null -w "%{size_download}" "${TARGET_HOST}/products/fast/123" || echo "0")
    miss_savings_json=$(./benchmarks/lib/payload_savings_parser.sh "$miss_size" "$miss_size")
    echo "Extracting payload savings for L1 Hit..."
    l1_size=$(curl -s -o /dev/null -w "%{size_download}" "${TARGET_HOST}/products/fast/123" || echo "0")
    l1_savings_json=$(./benchmarks/lib/payload_savings_parser.sh "$l1_size" "$l1_size")

    jq -n \
      --arg ts "$TIMESTAMP" \
      --arg l2 "$L2_KEY" \
      --arg m_tr "$miss_total_reqs" \
      --argjson m_lat "$miss_latency_json" \
      --argjson m_tp "$miss_throughput_json" \
      --argjson m_st "$miss_stats_json" \
      --argjson m_sv "$miss_savings_json" \
      --argjson m_mock "$miss_metrics_json" \
      --arg m_c "$miss_calls" \
      --arg l1_tr "$l1_total_reqs" \
      --argjson l1_lat "$l1_latency_json" \
      --argjson l1_tp "$l1_throughput_json" \
      --argjson l1_st "$l1_stats_json" \
      --argjson l1_sv "$l1_savings_json" \
      --argjson l1_mock "$l1_metrics_json" \
      --arg l1_c "$l1_calls" \
      '{phase: "l2_ready", timestamp: $ts, l2_key: $l2,
        miss_total_reqs: $m_tr, miss_latency_json: $m_lat, miss_throughput_json: $m_tp,
        miss_stats_json: $m_st, miss_savings_json: $m_sv, miss_mock_api_metrics_json: $m_mock, miss_calls: $m_c,
        l1_total_reqs: $l1_tr, l1_latency_json: $l1_lat, l1_throughput_json: $l1_tp,
        l1_stats_json: $l1_st, l1_savings_json: $l1_sv, l1_mock_api_metrics_json: $l1_mock, l1_calls: $l1_c}' \
      > "$CHECKPOINT_FILE"

    rm -f "$WRK_MISS_OUT" "$WRK_L1_OUT" /tmp/miss_headers.txt /tmp/l1_headers.txt

    echo ""
    echo "=================================================="
    echo "PHASE 1 COMPLETE."
    echo "Checkpoint successfully written to build folder."
    echo "ACTION REQUIRED: Please restart bytetaper-extproc-cache-a to clear its memory L1 cache."
    echo "=================================================="
    exit 0
else
    echo "--------------------------------------------------"
    echo "Starting Phase 2 (L2 Hit after Restart profile)"
    echo "--------------------------------------------------"

    TIMESTAMP=$(jq -r '.timestamp' "$CHECKPOINT_FILE")
    L2_KEY=$(jq -r '.l2_key' "$CHECKPOINT_FILE")
    miss_total_reqs=$(jq -r '.miss_total_reqs' "$CHECKPOINT_FILE")
    miss_latency_json=$(jq -c '.miss_latency_json' "$CHECKPOINT_FILE")
    miss_throughput_json=$(jq -c '.miss_throughput_json' "$CHECKPOINT_FILE")
    miss_stats_json=$(jq -c '.miss_stats_json' "$CHECKPOINT_FILE")
    miss_savings_json=$(jq -c '.miss_savings_json' "$CHECKPOINT_FILE")
    miss_metrics_json=$(jq -c '.miss_mock_api_metrics_json' "$CHECKPOINT_FILE")
    miss_calls=$(jq -r '.miss_calls' "$CHECKPOINT_FILE")
    l1_total_reqs=$(jq -r '.l1_total_reqs' "$CHECKPOINT_FILE")
    l1_latency_json=$(jq -c '.l1_latency_json' "$CHECKPOINT_FILE")
    l1_throughput_json=$(jq -c '.l1_throughput_json' "$CHECKPOINT_FILE")
    l1_stats_json=$(jq -c '.l1_stats_json' "$CHECKPOINT_FILE")
    l1_savings_json=$(jq -c '.l1_savings_json' "$CHECKPOINT_FILE")
    l1_metrics_json=$(jq -c '.l1_mock_api_metrics_json' "$CHECKPOINT_FILE")
    l1_calls=$(jq -r '.l1_calls' "$CHECKPOINT_FILE")

    REPORT_FILE="${REPORT_DIR}/benchmark_results_${TIMESTAMP}_${SCENARIO}.txt"

    initial_l2_hits=$(curl -s "${METRICS_HOST}" | grep '^bytetaper_runtime_l2_async_lookup_hit_total ' | awk '{print $2}' || echo "0")
    if [ -z "$initial_l2_hits" ]; then initial_l2_hits=0; fi

    mock_api_reset_metrics "$MOCK_API"

    curl -s "${TARGET_HOST}/products/${L2_KEY}" > /dev/null
    sleep 0.2
    curl -sD /tmp/l2_headers.txt "${TARGET_HOST}/products/${L2_KEY}" > /dev/null

    l2_metrics_json=$(mock_api_snapshot "$MOCK_API")
    l2_calls=$(mock_api_upstream_requests "$l2_metrics_json")

    l2_cached=$(grep -i 'x-bytetaper-cached-response' /tmp/l2_headers.txt | tr -d '\r' | awk '{print $2}' || echo "false")
    l2_layer=$(grep -i 'x-bytetaper-cache-layer' /tmp/l2_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")

    final_l2_hits=$(curl -s "${METRICS_HOST}" | grep '^bytetaper_runtime_l2_async_lookup_hit_total ' | awk '{print $2}' || echo "0")
    if [ -z "$final_l2_hits" ]; then final_l2_hits=0; fi
    l2_metric_delta=$((final_l2_hits - initial_l2_hits))

    echo "Restarted Instance Cached-Header: ${l2_cached}"
    echo "Restarted Instance Cache-Layer: ${l2_layer}"
    echo "Upstream Call Delta for Restarted L2 sequence: ${l2_calls}"
    echo "Restarted L2 Async Lookup Hit Metric Delta: ${l2_metric_delta}"
    echo ""

    if [ "${l2_calls}" -ne 1 ]; then
        echo "ERROR: Expected exactly 1 upstream call for L2 async promotion sequence, got ${l2_calls}!"
        exit 1
    fi
    if [ "${l2_metric_delta}" -lt 1 ]; then
        echo "ERROR: Expected at least 1 background L2 async hit metric increment, got ${l2_metric_delta}!"
        exit 1
    fi

    echo "Running wrk load test on restarted Server..."
    WRK_L2_OUT=$(mktemp)
    wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "${TARGET_HOST}/products/${L2_KEY}" | tee "$WRK_L2_OUT"

    echo "Extracting L2 Hit latency JSON..."
    l2_latency_json=$(./benchmarks/lib/latency_parser.sh "$WRK_L2_OUT")
    echo "Extracting L2 Hit throughput JSON..."
    l2_throughput_json=$(./benchmarks/lib/throughput_parser.sh "$WRK_L2_OUT")
    l2_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_L2_OUT" | awk '{print $1}' || echo "0")

    echo "Extracting container stats for L2 Hit..."
    l2_stats_json=$(./benchmarks/lib/container_stats.sh all)

    echo "Extracting payload savings for L2 Hit..."
    l2_size=$(curl -s -o /dev/null -w "%{size_download}" "${TARGET_HOST}/products/${L2_KEY}" || echo "0")
    l2_savings_json=$(./benchmarks/lib/payload_savings_parser.sh "$l2_size" "$l2_size")

    echo "Capturing startup configuration gauges..."
    METRICS_URL="${METRICS_CACHE_URL:-http://bytetaper-extproc-cache-a:18081}"
    metrics_raw=$(curl -s "${METRICS_URL}/metrics" || echo "")
    worker_count=$(echo "$metrics_raw" | grep "bytetaper_worker_count_effective" | grep -v "#" | awk '{print $2}' || echo "0")
    lookup_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_lookup_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
    store_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_store_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
    async_store_max_body=$(echo "$metrics_raw" | grep "bytetaper_worker_async_store_max_body_size_effective" | grep -v "#" | awk '{print $2}' || echo "0")
    l2_block_cache_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_block_cache_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
    l2_write_buffer_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_write_buffer_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
    l2_max_background_jobs=$(echo "$metrics_raw" | grep "bytetaper_l2_max_background_jobs_effective" | grep -v "#" | awk '{print $2}' || echo "0")

    {
        echo "=== ByteTaper Benchmark Execution ==="
        echo "Scenario: $SCENARIO"
        echo "Time: $(date)"
        echo "Target Host: $TARGET_HOST"
        echo "Mock API Host: $MOCK_API"
        echo "Config Gauges JSON: {\"worker_count\": ${worker_count:-0}, \"lookup_quota\": ${lookup_quota:-0}, \"store_quota\": ${store_quota:-0}, \"async_store_max_body\": ${async_store_max_body:-0}, \"l2_block_cache_mb\": ${l2_block_cache_mb:-0}, \"l2_write_buffer_mb\": ${l2_write_buffer_mb:-0}, \"l2_max_background_jobs\": ${l2_max_background_jobs:-0}}"
        echo ""
        echo "=== System Information ==="
        echo "OS: $(uname -snrmo)"
        echo "CPU cores: $(nproc)"
        echo "Memory Total: $(grep MemTotal /proc/meminfo | awk '{print $2/1024 " MB"}' || echo 'N/A')"
        echo ""
        echo "=== Parsed Cache Hit Scenario Metrics ==="
        echo "Cache Miss Total Requests (10s): ${miss_total_reqs}"
        echo "Cache Miss Latency JSON: ${miss_latency_json}"
        echo "Cache Miss Throughput JSON: ${miss_throughput_json}"
        echo "Cache Miss Container Stats JSON: ${miss_stats_json}"
        echo "Cache Miss Payload Savings JSON: ${miss_savings_json}"
        echo "Cache Miss Upstream Call Delta: ${miss_calls}"
        echo "Cache Miss Mock API Metrics JSON: ${miss_metrics_json}"
        echo "Cache Miss Cache-Layer Signal: None"
        echo ""
        echo "L1 Cache Hit Total Requests (10s): ${l1_total_reqs}"
        echo "L1 Cache Hit Latency JSON: ${l1_latency_json}"
        echo "L1 Cache Hit Throughput JSON: ${l1_throughput_json}"
        echo "L1 Cache Hit Container Stats JSON: ${l1_stats_json}"
        echo "L1 Cache Hit Payload Savings JSON: ${l1_savings_json}"
        echo "L1 Cache Hit Upstream Call Delta: ${l1_calls}"
        echo "L1 Cache Hit Mock API Metrics JSON: ${l1_metrics_json}"
        echo "L1 Cache Hit Cache-Layer Signal: L1 Header"
        echo ""
        echo "L2 Shared Hit Total Requests (10s): ${l2_total_reqs}"
        echo "L2 Shared Hit Latency JSON: ${l2_latency_json}"
        echo "L2 Shared Hit Throughput JSON: ${l2_throughput_json}"
        echo "L2 Shared Hit Container Stats JSON: ${l2_stats_json}"
        echo "L2 Shared Hit Payload Savings JSON: ${l2_savings_json}"
        echo "L2 Shared Hit Upstream Call Delta: ${l2_calls} (Async Lookup first request bypass)"
        echo "L2 Shared Hit Mock API Metrics JSON: ${l2_metrics_json}"
        echo "L2 Shared Hit Cache-Layer Signal: L2 Metric Delta (+${l2_metric_delta})"
    } > "$REPORT_FILE"

    rm -f "$WRK_L2_OUT" /tmp/l2_headers.txt "$CHECKPOINT_FILE"

    ./benchmarks/report/validate.sh "$REPORT_FILE"
    ./benchmarks/report/generate_json_report.sh "$REPORT_FILE"
    ./benchmarks/report/generate_markdown_report.sh "${REPORT_FILE%.txt}.json"
    ./benchmarks/report/check_thresholds.sh "${REPORT_FILE%.txt}.json"

    echo ""
    echo "=================================================="
    echo "PHASE 2 COMPLETE. REPORT GENERATED SUCCESSFULLY."
    echo "=================================================="
    cat "$REPORT_FILE"
fi

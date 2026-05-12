#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Configuration
SCENARIO="pagination_guardrail"
TARGET_HOST="${ENVOY_PAGINATION_URL:-http://envoy-bytetaper-pagination:10000}"
REPORT_DIR="reports/benchmarks"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${REPORT_DIR}/benchmark_results_${TIMESTAMP}_${SCENARIO}.txt"

mkdir -p "$REPORT_DIR"

# Collect system info
{
    echo "=== ByteTaper Benchmark Execution ==="
    echo "Scenario: $SCENARIO"
    echo "Time: $(date)"
    echo "Target Host: $TARGET_HOST"
    echo ""
    echo "=== System Information ==="
    echo "OS: $(uname -snrmo)"
    echo "CPU cores: $(nproc)"
    echo "Memory Total: $(grep MemTotal /proc/meminfo | awk '{print $2/1024 " MB"}' || echo 'N/A')"
    echo ""
} > "$REPORT_FILE"

# Print system info to console
cat "$REPORT_FILE"

# Check target availability
echo "Checking target availability..."
if ! curl -s --fail "${TARGET_HOST}/orders" > /dev/null; then
    echo "ERROR: Target endpoint ${TARGET_HOST}/orders is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi

echo "Targets are UP."

echo "Capturing startup configuration gauges..."
METRICS_HOST="${METRICS_PAGINATION_URL:-http://bytetaper-extproc-pagination:18081}"
metrics_raw=$(curl -s "${METRICS_HOST}/metrics" || echo "")
worker_count=$(echo "$metrics_raw" | grep "bytetaper_worker_count_effective" | grep -v "#" | awk '{print $2}' || echo "0")
lookup_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_lookup_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
store_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_store_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
async_store_max_body=$(echo "$metrics_raw" | grep "bytetaper_worker_async_store_max_body_size_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_block_cache_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_block_cache_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_write_buffer_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_write_buffer_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_max_background_jobs=$(echo "$metrics_raw" | grep "bytetaper_l2_max_background_jobs_effective" | grep -v "#" | awk '{print $2}' || echo "0")

echo "Config Gauges JSON: {\"worker_count\": ${worker_count:-0}, \"lookup_quota\": ${lookup_quota:-0}, \"store_quota\": ${store_quota:-0}, \"async_store_max_body\": ${async_store_max_body:-0}, \"l2_block_cache_mb\": ${l2_block_cache_mb:-0}, \"l2_write_buffer_mb\": ${l2_write_buffer_mb:-0}, \"l2_max_background_jobs\": ${l2_max_background_jobs:-0}}" >> "$REPORT_FILE"

# --------------------------------------------------
# Leg A: Missing Limit (GET /orders -> Inject limit=50)
# --------------------------------------------------
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg A: Missing Limit (GET /orders)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LEGA_URL="${TARGET_HOST}/orders"

# Preflight request
curl -sD /tmp/lega_headers.txt -o /tmp/lega_body.txt "${LEGA_URL}"

# Parse headers and body
lega_applied=$(grep -i 'x-bytetaper-pagination-applied' /tmp/lega_headers.txt | tr -d '\r' | awk '{print $2}' || echo "false")
lega_reason=$(grep -i 'x-bytetaper-pagination-reason' /tmp/lega_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
lega_limit_header=$(grep -i 'x-bytetaper-pagination-limit' /tmp/lega_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
lega_received_limit=$(jq -r '.received_limit' /tmp/lega_body.txt || echo "None")
lega_bytes=$(wc -c < /tmp/lega_body.txt | tr -d ' ' || echo "0")

echo "Pagination Applied Header: ${lega_applied}" | tee -a "$REPORT_FILE"
echo "Pagination Reason Header: ${lega_reason}" | tee -a "$REPORT_FILE"
echo "Pagination Limit Header: ${lega_limit_header}" | tee -a "$REPORT_FILE"
echo "Upstream Received Limit (Echo): ${lega_received_limit}" | tee -a "$REPORT_FILE"
echo "Response Payload Size: ${lega_bytes} bytes" | tee -a "$REPORT_FILE"

# Assertions
if [ "${lega_applied}" != "true" ]; then
    echo "ERROR: Expected pagination applied in Leg A!"
    exit 1
fi
if [ "${lega_reason}" != "missing_limit" ]; then
    echo "ERROR: Expected reason 'missing_limit' in Leg A, got '${lega_reason}'!"
    exit 1
fi
if [ "${lega_received_limit}" != "50" ]; then
    echo "ERROR: Upstream did not receive the expected default limit '50'! Got '${lega_received_limit}'"
    exit 1
fi

echo "" | tee -a "$REPORT_FILE"
echo "Running wrk load test on Leg A (Missing Limit)..."
WRK_LEGA_OUT=$(mktemp)
wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "${LEGA_URL}" | tee "$WRK_LEGA_OUT"
cat "$WRK_LEGA_OUT" >> "$REPORT_FILE"

# Extract JSON latency and throughput metrics for Leg A
echo "Extracting JSON latency metrics for Leg A..."
JSON_LEGA_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_LEGA_OUT")
echo "Extracting JSON throughput metrics for Leg A..."
JSON_LEGA_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_LEGA_OUT")

# Sleep 3 seconds to let connections cool down and socket pool reset
echo "Cooling down socket pools..."
sleep 3

# --------------------------------------------------
# Leg B: Excessive Limit (GET /orders?limit=5000 -> Cap to limit=500)
# --------------------------------------------------
echo "" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg B: Excessive Limit (GET /orders?limit=5000)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LEGB_URL="${TARGET_HOST}/orders?limit=5000"

# Preflight request
curl -sD /tmp/legb_headers.txt -o /tmp/legb_body.txt "${LEGB_URL}"

# Parse headers and body
legb_applied=$(grep -i 'x-bytetaper-pagination-applied' /tmp/legb_headers.txt | tr -d '\r' | awk '{print $2}' || echo "false")
legb_reason=$(grep -i 'x-bytetaper-pagination-reason' /tmp/legb_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legb_limit_header=$(grep -i 'x-bytetaper-pagination-limit' /tmp/legb_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legb_received_limit=$(jq -r '.received_limit' /tmp/legb_body.txt || echo "None")
legb_bytes=$(wc -c < /tmp/legb_body.txt | tr -d ' ' || echo "0")

echo "Pagination Applied Header: ${legb_applied}" | tee -a "$REPORT_FILE"
echo "Pagination Reason Header: ${legb_reason}" | tee -a "$REPORT_FILE"
echo "Pagination Limit Header: ${legb_limit_header}" | tee -a "$REPORT_FILE"
echo "Upstream Received Limit (Echo): ${legb_received_limit}" | tee -a "$REPORT_FILE"
echo "Response Payload Size: ${legb_bytes} bytes" | tee -a "$REPORT_FILE"

# Assertions
if [ "${legb_applied}" != "true" ]; then
    echo "ERROR: Expected pagination applied in Leg B!"
    exit 1
fi
if [ "${legb_reason}" != "limit_exceeds_max" ]; then
    echo "ERROR: Expected reason 'limit_exceeds_max' in Leg B, got '${legb_reason}'!"
    exit 1
fi
if [ "${legb_received_limit}" != "500" ]; then
    echo "ERROR: Upstream did not receive the expected max limit '500'! Got '${legb_received_limit}'"
    exit 1
fi

echo "" | tee -a "$REPORT_FILE"
echo "Running wrk load test on Leg B (Excessive Limit)..."
WRK_LEGB_OUT=$(mktemp)
wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "${LEGB_URL}" | tee "$WRK_LEGB_OUT"
cat "$WRK_LEGB_OUT" >> "$REPORT_FILE"

# Extract JSON latency and throughput metrics for Leg B
echo "Extracting JSON latency metrics for Leg B..."
JSON_LEGB_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_LEGB_OUT")
echo "Extracting JSON throughput metrics for Leg B..."
JSON_LEGB_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_LEGB_OUT")

# --------------------------------------------------
# Report Parsing & Compilation
# --------------------------------------------------

# Parse Leg A wrk results
lega_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_LEGA_OUT" | awk '{print $1}' || echo "0")
lega_non_2xx=$(grep -E "Non-2xx or 3xx responses:" "$WRK_LEGA_OUT" | awk '{print $5}' || echo "0")
if [ -z "$lega_non_2xx" ]; then lega_non_2xx=0; fi
lega_success=$((lega_total_reqs - lega_non_2xx))
lega_transfer=$(grep -E '^[[:space:]]*Transfer/sec:' "$WRK_LEGA_OUT" | awk '{print $2 " " $3}' || echo "N/A")

# Parse Leg B wrk results
legb_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_LEGB_OUT" | awk '{print $1}' || echo "0")
legb_non_2xx=$(grep -E "Non-2xx or 3xx responses:" "$WRK_LEGB_OUT" | awk '{print $5}' || echo "0")
if [ -z "$legb_non_2xx" ]; then legb_non_2xx=0; fi
legb_success=$((legb_total_reqs - legb_non_2xx))
legb_transfer=$(grep -E '^[[:space:]]*Transfer/sec:' "$WRK_LEGB_OUT" | awk '{print $2 " " $3}' || echo "N/A")

# Extract container stats for Leg A and Leg B
echo "Extracting container stats..."
JSON_LEGA_STATS=$(./benchmarks/lib/container_stats.sh all)
JSON_LEGB_STATS=$(./benchmarks/lib/container_stats.sh all)

# Extract response payload savings for Leg A and Leg B
echo "Extracting payload savings..."
JSON_LEGA_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$lega_bytes" "$lega_bytes")
JSON_LEGB_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$legb_bytes" "$legb_bytes")

echo "" >> "$REPORT_FILE"
echo "=== Parsed Scenario Metrics ===" >> "$REPORT_FILE"
{
    echo "Leg A (Missing Limit) - Expected Mutated Limit: 50"
    echo "Leg A Upstream Received Limit: ${lega_received_limit}"
    echo "Leg A Decision Reason: ${lega_reason}"
    echo "Leg A Payload Size: ${lega_bytes} bytes"
    echo "Leg A Total Requests (10s): ${lega_total_reqs}"
    echo "Leg A Successful Requests: ${lega_success}"
    echo "Leg A Transfer Rate: ${lega_transfer}"
    echo "Leg A Latency JSON: ${JSON_LEGA_LATENCY}"
    echo "Leg A Throughput JSON: ${JSON_LEGA_THROUGHPUT}"
    echo "Leg A Container Stats JSON: ${JSON_LEGA_STATS}"
    echo "Leg A Payload Savings JSON: ${JSON_LEGA_SAVINGS}"
    echo ""
    echo "Leg B (Excessive Limit) - Expected Mutated Limit: 500"
    echo "Leg B Upstream Received Limit: ${legb_received_limit}"
    echo "Leg B Decision Reason: ${legb_reason}"
    echo "Leg B Payload Size: ${legb_bytes} bytes"
    echo "Leg B Total Requests (10s): ${legb_total_reqs}"
    echo "Leg B Successful Requests: ${legb_success}"
    echo "Leg B Transfer Rate: ${legb_transfer}"
    echo "Leg B Latency JSON: ${JSON_LEGB_LATENCY}"
    echo "Leg B Throughput JSON: ${JSON_LEGB_THROUGHPUT}"
    echo "Leg B Container Stats JSON: ${JSON_LEGB_STATS}"
    echo "Leg B Payload Savings JSON: ${JSON_LEGB_SAVINGS}"
} >> "$REPORT_FILE"

# Baseline Comparison Section
echo "" >> "$REPORT_FILE"
echo "=== Baseline Comparison ===" >> "$REPORT_FILE"

latest_envoy_only=$(ls -t reports/benchmarks/benchmark_results_*_envoy_only.txt 2>/dev/null | head -n 1 || true)
latest_bytetaper_observe=$(ls -t reports/benchmarks/benchmark_results_*_bytetaper_observe.txt 2>/dev/null | head -n 1 || true)

if [ -n "${latest_envoy_only}" ]; then
    echo "Baseline Scenario (envoy_only) File: $(basename "${latest_envoy_only}")" >> "$REPORT_FILE"
    grep -E '^(Total Requests:|Success Count:|Latency p50:|Latency p99:)' "${latest_envoy_only}" >> "$REPORT_FILE" || true
else
    echo "Baseline Scenario (envoy_only): UNAVAILABLE" >> "$REPORT_FILE"
fi

echo "" >> "$REPORT_FILE"

if [ -n "${latest_bytetaper_observe}" ]; then
    echo "Observe Scenario (bytetaper_observe) File: $(basename "${latest_bytetaper_observe}")" >> "$REPORT_FILE"
    grep -E '^(Total Requests:|Success Count:|Latency p50:|Latency p99:)' "${latest_bytetaper_observe}" >> "$REPORT_FILE" || true
else
    echo "Observe Scenario (bytetaper_observe): UNAVAILABLE" >> "$REPORT_FILE"
fi

# Cleanup
rm -f "$WRK_LEGA_OUT" "$WRK_LEGB_OUT" /tmp/lega_headers.txt /tmp/lega_body.txt /tmp/legb_headers.txt /tmp/legb_body.txt

# Validate report integrity
./benchmarks/report/validate.sh "$REPORT_FILE"

# Compile JSON report
./benchmarks/report/generate_json_report.sh "$REPORT_FILE"

# Compile Markdown report
./benchmarks/report/generate_markdown_report.sh "${REPORT_FILE%.txt}.json"

# Validate performance thresholds
./benchmarks/report/check_thresholds.sh "${REPORT_FILE%.txt}.json"

echo ""
echo "Benchmark complete."
echo "Results saved to: $REPORT_FILE"
cat "$REPORT_FILE" | tail -n 30

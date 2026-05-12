#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Configuration
SCENARIO="bytetaper_observe"
TARGET_URL="${ENVOY_OBSERVE_URL:-http://envoy-bytetaper-observe:10000}/api/v1/cached/fast/bench"
ENVOY_BASELINE_URL="${ENVOY_BASELINE_URL:-http://envoy-baseline:10000}/api/v1/cached/fast/bench"
REPORT_DIR="reports/benchmarks"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${REPORT_DIR}/benchmark_results_${TIMESTAMP}_${SCENARIO}.txt"

mkdir -p "$REPORT_DIR"

# Collect system info
{
    echo "=== ByteTaper Benchmark Execution ==="
    echo "Scenario: $SCENARIO"
    echo "Time: $(date)"
    echo "Target: $TARGET_URL"
    echo ""
    echo "=== System Information ==="
    echo "OS: $(uname -snrmo)"
    echo "CPU cores: $(nproc)"
    echo "Memory Total: $(grep MemTotal /proc/meminfo | awk '{print $2/1024 " MB"}')"
    echo ""
} > "$REPORT_FILE"

# Print system info to console
cat "$REPORT_FILE"

# Check target availability
echo "Checking target availability..."
if ! curl -s --fail "$TARGET_URL" > /dev/null; then
    echo "ERROR: Benchmark target $TARGET_URL is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi

if ! curl -s --fail "$ENVOY_BASELINE_URL" > /dev/null; then
    echo "ERROR: Baseline target $ENVOY_BASELINE_URL is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi

echo "Targets are UP."

echo "Capturing startup configuration gauges..."
METRICS_HOST="${METRICS_OBSERVE_URL:-http://bytetaper-extproc-observe:18081}"
metrics_raw=$(curl -s "${METRICS_HOST}/metrics" || echo "")
worker_count=$(echo "$metrics_raw" | grep "bytetaper_worker_count_effective" | grep -v "#" | awk '{print $2}' || echo "0")
lookup_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_lookup_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
store_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_store_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
async_store_max_body=$(echo "$metrics_raw" | grep "bytetaper_worker_async_store_max_body_size_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_block_cache_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_block_cache_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_write_buffer_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_write_buffer_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_max_background_jobs=$(echo "$metrics_raw" | grep "bytetaper_l2_max_background_jobs_effective" | grep -v "#" | awk '{print $2}' || echo "0")

echo "Config Gauges JSON: {\"worker_count\": ${worker_count:-0}, \"lookup_quota\": ${lookup_quota:-0}, \"store_quota\": ${store_quota:-0}, \"async_store_max_body\": ${async_store_max_body:-0}, \"l2_block_cache_mb\": ${l2_block_cache_mb:-0}, \"l2_write_buffer_mb\": ${l2_write_buffer_mb:-0}, \"l2_max_background_jobs\": ${l2_max_background_jobs:-0}}" >> "$REPORT_FILE"

# Compare body checksum/sample between envoy-baseline and envoy-bytetaper-observe targets
echo "Comparing response body between baseline and observe targets..."
body_baseline=$(curl -sSf "$ENVOY_BASELINE_URL")
body_observe=$(curl -sSf "$TARGET_URL")

if [ "${body_baseline}" != "${body_observe}" ]; then
    echo "ERROR: Response body difference detected between baseline and observe targets!"
    echo "Baseline response: ${body_baseline}"
    echo "Observe response: ${body_observe}"
    exit 1
fi
echo "Response body equivalence verified successfully (100% identical)."

echo "Starting wrk load test on observe target..."

# Run wrk and capture raw output to a temporary file
WRK_OUT=$(mktemp)
wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "$TARGET_URL" | tee "$WRK_OUT"

# Append raw wrk output to report file
cat "$WRK_OUT" >> "$REPORT_FILE"

# Extract JSON latency percentiles
echo "Extracting JSON latency metrics..."
JSON_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_OUT")

# Parse metrics
echo "" >> "$REPORT_FILE"
echo "=== Parsed Scenario Metrics ===" >> "$REPORT_FILE"

# Total requests
total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_OUT" | awk '{print $1}' || echo "0")
if [ -z "$total_reqs" ]; then
    total_reqs=0
fi

# Non-2xx/3xx if present
non_2xx_3xx=$(grep -E "Non-2xx or 3xx responses:" "$WRK_OUT" | awk '{print $5}' || echo "0")
if [ -z "$non_2xx_3xx" ]; then
    non_2xx_3xx=0
fi

# Success count
success_count=$((total_reqs - non_2xx_3xx))

# Bytes read & transfer rate
bytes_read=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_OUT" | awk -F', ' '{print $2}' || echo "0")
transfer_rate=$(grep -E "^[[:space:]]*Transfer/sec:" "$WRK_OUT" | awk '{print $2}' || echo "0")

# Extract throughput JSON
echo "Extracting throughput JSON..."
JSON_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_OUT")

# Extract container stats
echo "Extracting container stats..."
JSON_STATS=$(./benchmarks/lib/container_stats.sh all)

# Extract response payload savings
echo "Extracting payload savings..."
resp_size=$(curl -s -o /dev/null -w "%{size_download}" "$TARGET_URL" || echo "0")
JSON_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$resp_size" "$resp_size")

{
    echo "Total Requests: $total_reqs"
    echo "Success Count: $success_count"
    echo "Bytes Read: $bytes_read"
    echo "Transfer Rate: $transfer_rate"
    echo "Body Equivalence Match: YES"
    echo "Latency JSON: $JSON_LATENCY"
    echo "Throughput JSON: $JSON_THROUGHPUT"
    echo "Container Stats JSON: $JSON_STATS"
    echo "Payload Savings JSON: $JSON_SAVINGS"
} >> "$REPORT_FILE"

# Cleanup
rm -f "$WRK_OUT"

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
cat "$REPORT_FILE" | tail -n 15

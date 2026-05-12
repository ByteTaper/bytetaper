#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Configuration
SCENARIO="bytetaper_field_filtering"
TARGET_HOST="${ENVOY_FIELD_FILTERING_URL:-http://envoy-bytetaper-field-filtering:10000}"
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
    echo "Memory Total: $(grep MemTotal /proc/meminfo | awk '{print $2/1024 " MB"}')"
    echo ""
} > "$REPORT_FILE"

# Print system info to console
cat "$REPORT_FILE"

# Check target availability
echo "Checking target availability..."
if ! curl -s --fail "${TARGET_HOST}/medium-json?fields=id,name,price" > /dev/null; then
    echo "ERROR: Target endpoint ${TARGET_HOST}/medium-json is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi
if ! curl -s --fail "${TARGET_HOST}/large-json?fields=id,name,price" > /dev/null; then
    echo "ERROR: Target endpoint ${TARGET_HOST}/large-json is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi

echo "Targets are UP."

echo "Capturing startup configuration gauges..."
METRICS_HOST="${METRICS_FIELD_FILTERING_URL:-http://bytetaper-extproc-field-filtering:18081}"
metrics_raw=$(curl -s "${METRICS_HOST}/metrics" || echo "")
worker_count=$(echo "$metrics_raw" | grep "bytetaper_worker_count_effective" | grep -v "#" | awk '{print $2}' || echo "0")
lookup_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_lookup_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
store_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_store_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
async_store_max_body=$(echo "$metrics_raw" | grep "bytetaper_worker_async_store_max_body_size_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_block_cache_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_block_cache_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_write_buffer_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_write_buffer_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_max_background_jobs=$(echo "$metrics_raw" | grep "bytetaper_l2_max_background_jobs_effective" | grep -v "#" | awk '{print $2}' || echo "0")

echo "Config Gauges JSON: {\"worker_count\": ${worker_count:-0}, \"lookup_quota\": ${lookup_quota:-0}, \"store_quota\": ${store_quota:-0}, \"async_store_max_body\": ${async_store_max_body:-0}, \"l2_block_cache_mb\": ${l2_block_cache_mb:-0}, \"l2_write_buffer_mb\": ${l2_write_buffer_mb:-0}, \"l2_max_background_jobs\": ${l2_max_background_jobs:-0}}" >> "$REPORT_FILE"

# Leg 1: Medium Payload Field Filtering
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg 1: Medium Payload Field Filtering (?fields=id,name,price)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

MED_URL="${TARGET_HOST}/medium-json?fields=id,name,price"

# Capture response size metrics
curl -sD /tmp/med_headers.txt "${MED_URL}" > /dev/null
med_orig=$(grep -i 'x-bytetaper-original-response-bytes' /tmp/med_headers.txt | tr -d '\r' | awk '{print $2}')
med_opt=$(grep -i 'x-bytetaper-optimized-response-bytes' /tmp/med_headers.txt | tr -d '\r' | awk '{print $2}')

if [ -z "${med_orig}" ] || [ -z "${med_opt}" ]; then
    echo "ERROR: Missing ByteTaper mutation headers in Leg 1!"
    exit 1
fi

med_saved=$((med_orig - med_opt))
med_ratio=$(awk "BEGIN {printf \"%.2f%%\", (${med_saved} / ${med_orig}) * 100}")

echo "Original Size: ${med_orig} bytes" | tee -a "$REPORT_FILE"
echo "Optimized Size: ${med_opt} bytes" | tee -a "$REPORT_FILE"
echo "Bytes Saved: ${med_saved} bytes" | tee -a "$REPORT_FILE"
echo "Reduction Ratio: ${med_ratio}" | tee -a "$REPORT_FILE"
echo "" | tee -a "$REPORT_FILE"

echo "Running wrk load test on Leg 1..."
WRK_MED_OUT=$(mktemp)
wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "${MED_URL}" | tee "$WRK_MED_OUT"
cat "$WRK_MED_OUT" >> "$REPORT_FILE"

# Extract JSON latency and throughput metrics for Leg 1
echo "Extracting JSON latency metrics for Leg 1..."
JSON_MED_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_MED_OUT")
echo "Extracting JSON throughput metrics for Leg 1..."
JSON_MED_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_MED_OUT")

# Leg 2: Large Payload Field Filtering
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg 2: Large Payload Field Filtering (?fields=id,name,price)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LARGE_URL="${TARGET_HOST}/large-json?fields=id,name,price"

# Capture response size metrics
curl -sD /tmp/large_headers.txt "${LARGE_URL}" > /dev/null
large_orig=$(grep -i 'x-bytetaper-original-response-bytes' /tmp/large_headers.txt | tr -d '\r' | awk '{print $2}')
large_opt=$(grep -i 'x-bytetaper-optimized-response-bytes' /tmp/large_headers.txt | tr -d '\r' | awk '{print $2}')

if [ -z "${large_orig}" ] || [ -z "${large_opt}" ]; then
    echo "ERROR: Missing ByteTaper mutation headers in Leg 2!"
    exit 1
fi

large_saved=$((large_orig - large_opt))
large_ratio=$(awk "BEGIN {printf \"%.2f%%\", (${large_saved} / ${large_orig}) * 100}")

echo "Original Size: ${large_orig} bytes" | tee -a "$REPORT_FILE"
echo "Optimized Size: ${large_opt} bytes" | tee -a "$REPORT_FILE"
echo "Bytes Saved: ${large_saved} bytes" | tee -a "$REPORT_FILE"
echo "Reduction Ratio: ${large_ratio}" | tee -a "$REPORT_FILE"
echo "" | tee -a "$REPORT_FILE"

echo "Running wrk load test on Leg 2..."
WRK_LARGE_OUT=$(mktemp)
wrk -t2 -c10 -d10s -s benchmarks/lib/latency_reporter.lua --latency "${LARGE_URL}" | tee "$WRK_LARGE_OUT"
cat "$WRK_LARGE_OUT" >> "$REPORT_FILE"

# Extract JSON latency and throughput metrics for Leg 2
echo "Extracting JSON latency metrics for Leg 2..."
JSON_LARGE_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_LARGE_OUT")
echo "Extracting JSON throughput metrics for Leg 2..."
JSON_LARGE_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_LARGE_OUT")

# Parse metrics for Leg 1
med_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_MED_OUT" | awk '{print $1}' || echo "0")
med_non_2xx=$(grep -E "Non-2xx or 3xx responses:" "$WRK_MED_OUT" | awk '{print $5}' || echo "0")
if [ -z "$med_non_2xx" ]; then med_non_2xx=0; fi
med_success=$((med_total_reqs - med_non_2xx))

# Parse metrics for Leg 2
large_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_LARGE_OUT" | awk '{print $1}' || echo "0")
large_non_2xx=$(grep -E "Non-2xx or 3xx responses:" "$WRK_LARGE_OUT" | awk '{print $5}' || echo "0")
if [ -z "$large_non_2xx" ]; then large_non_2xx=0; fi
large_success=$((large_total_reqs - large_non_2xx))

# Extract container stats for Leg 1 and Leg 2
echo "Extracting container stats..."
JSON_MED_STATS=$(./benchmarks/lib/container_stats.sh all)
JSON_LARGE_STATS=$(./benchmarks/lib/container_stats.sh all)

# Extract response payload savings
echo "Extracting payload savings..."
JSON_MED_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$med_orig" "$med_opt")
JSON_LARGE_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$large_orig" "$large_opt")

# Write parsed metrics
echo "" >> "$REPORT_FILE"
echo "=== Parsed Scenario Metrics ===" >> "$REPORT_FILE"
{
    echo "Leg 1 (Medium JSON) Requests: ${med_total_reqs}"
    echo "Leg 1 Success Count: ${med_success}"
    echo "Leg 1 Bytes Saved: ${med_saved} bytes"
    echo "Leg 1 Reduction Ratio: ${med_ratio}"
    echo "Leg 1 Latency JSON: ${JSON_MED_LATENCY}"
    echo "Leg 1 Throughput JSON: ${JSON_MED_THROUGHPUT}"
    echo "Leg 1 Container Stats JSON: ${JSON_MED_STATS}"
    echo "Leg 1 Payload Savings JSON: ${JSON_MED_SAVINGS}"
    echo ""
    echo "Leg 2 (Large JSON) Requests: ${large_total_reqs}"
    echo "Leg 2 Success Count: ${large_success}"
    echo "Leg 2 Bytes Saved: ${large_saved} bytes"
    echo "Leg 2 Reduction Ratio: ${large_ratio}"
    echo "Leg 2 Latency JSON: ${JSON_LARGE_LATENCY}"
    echo "Leg 2 Throughput JSON: ${JSON_LARGE_THROUGHPUT}"
    echo "Leg 2 Container Stats JSON: ${JSON_LARGE_STATS}"
    echo "Leg 2 Payload Savings JSON: ${JSON_LARGE_SAVINGS}"
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
rm -f "$WRK_MED_OUT" "$WRK_LARGE_OUT" /tmp/med_headers.txt /tmp/large_headers.txt

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
cat "$REPORT_FILE" | tail -n 25

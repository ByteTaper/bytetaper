#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Configuration
SCENARIO="compression_coordination"
TARGET_HOST="${ENVOY_COMPRESSION_URL:-http://envoy-compression:10000}"
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
if ! curl -s --fail "${TARGET_HOST}/api/v1/large" > /dev/null; then
    echo "ERROR: Target endpoint ${TARGET_HOST}/api/v1/large is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi

echo "Targets are UP."

echo "Capturing startup configuration gauges..."
METRICS_HOST="${METRICS_COMPRESSION_URL:-http://bytetaper-extproc-compression:18081}"
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
# Leg A: Envoy Compression Only (Baseline)
# --------------------------------------------------
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg A: Envoy Compression Only (Baseline)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LEGA_URL="${TARGET_HOST}/api/v1/baseline-compress"

# Preflight request with gzip Accept-Encoding
curl -s -H "Accept-Encoding: gzip" -D /tmp/lega_headers.txt -o /tmp/lega_body.txt "${LEGA_URL}"

# Parse headers
lega_encoding=$(grep -i 'content-encoding' /tmp/lega_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
lega_candidate=$(grep -i 'x-bytetaper-compression-candidate' /tmp/lega_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
lega_bytes=$(wc -c < /tmp/lega_body.txt | tr -d ' ' || echo "0")

echo "Content-Encoding: ${lega_encoding}" | tee -a "$REPORT_FILE"
echo "ByteTaper Candidate Header: ${lega_candidate}" | tee -a "$REPORT_FILE"
echo "Response Payload Size (Compressed): ${lega_bytes} bytes" | tee -a "$REPORT_FILE"

# Assertions
if [[ "${lega_encoding}" != *"gzip"* ]]; then
    echo "ERROR: Expected Leg A response to be gzip compressed! Got Content-Encoding: '${lega_encoding}'"
    exit 1
fi
if [ "${lega_candidate}" != "None" ]; then
    echo "ERROR: Expected Leg A (baseline) to have no ByteTaper decision candidate header! Got: '${lega_candidate}'"
    exit 1
fi

echo "" | tee -a "$REPORT_FILE"
echo "Running wrk load test on Leg A (Envoy Compression Only)..."
WRK_LEGA_OUT=$(mktemp)
wrk -t2 -c10 -d10s -H "Accept-Encoding: gzip" -s benchmarks/lib/latency_reporter.lua --latency "${LEGA_URL}" | tee "$WRK_LEGA_OUT"
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
# Leg B: Envoy Compression + ByteTaper Decision
# --------------------------------------------------
echo "" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg B: Envoy Compression + ByteTaper Decision" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LEGB_URL="${TARGET_HOST}/api/v1/large"

# Preflight request with gzip Accept-Encoding
curl -s -H "Accept-Encoding: gzip" -D /tmp/legb_headers.txt -o /tmp/legb_body.txt "${LEGB_URL}"

# Parse headers
legb_encoding=$(grep -i 'content-encoding' /tmp/legb_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legb_candidate=$(grep -i 'x-bytetaper-compression-candidate' /tmp/legb_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legb_reason=$(grep -i 'x-bytetaper-compression-reason' /tmp/legb_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legb_algorithm=$(grep -i 'x-bytetaper-compression-algorithm-hint' /tmp/legb_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legb_bytes=$(wc -c < /tmp/legb_body.txt | tr -d ' ' || echo "0")

echo "Content-Encoding: ${legb_encoding}" | tee -a "$REPORT_FILE"
echo "ByteTaper Candidate Header: ${legb_candidate}" | tee -a "$REPORT_FILE"
echo "ByteTaper Skip Reason Header: ${legb_reason}" | tee -a "$REPORT_FILE"
echo "ByteTaper Algorithm Hint Header: ${legb_algorithm}" | tee -a "$REPORT_FILE"
echo "Response Payload Size (Compressed): ${legb_bytes} bytes" | tee -a "$REPORT_FILE"

# Assertions
if [[ "${legb_encoding}" != *"gzip"* ]]; then
    echo "ERROR: Expected Leg B response to be gzip compressed! Got Content-Encoding: '${legb_encoding}'"
    exit 1
fi
if [ "${legb_candidate}" != "true" ]; then
    echo "ERROR: Expected Leg B to be a compression candidate! Got: '${legb_candidate}'"
    exit 1
fi

echo "" | tee -a "$REPORT_FILE"
echo "Running wrk load test on Leg B (Envoy Compression + ByteTaper Decision)..."
WRK_LEGB_OUT=$(mktemp)
wrk -t2 -c10 -d10s -H "Accept-Encoding: gzip" -s benchmarks/lib/latency_reporter.lua --latency "${LEGB_URL}" | tee "$WRK_LEGB_OUT"
cat "$WRK_LEGB_OUT" >> "$REPORT_FILE"

# Extract JSON latency and throughput metrics for Leg B
echo "Extracting JSON latency metrics for Leg B..."
JSON_LEGB_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_LEGB_OUT")
echo "Extracting JSON throughput metrics for Leg B..."
JSON_LEGB_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_LEGB_OUT")

# Sleep 3 seconds to let connections cool down and socket pool reset
echo "Cooling down socket pools..."
sleep 3

# --------------------------------------------------
# Leg C: Small Response - Not Candidate
# --------------------------------------------------
echo "" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg C: Small Response - Not Candidate (GET /api/v1/small)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LEGC_URL="${TARGET_HOST}/api/v1/small"

# Preflight request with gzip Accept-Encoding
curl -s -H "Accept-Encoding: gzip" -D /tmp/legc_headers.txt -o /tmp/legc_body.txt "${LEGC_URL}"

# Parse headers
legc_encoding=$(grep -i 'content-encoding' /tmp/legc_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legc_candidate=$(grep -i 'x-bytetaper-compression-candidate' /tmp/legc_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legc_reason=$(grep -i 'x-bytetaper-compression-reason' /tmp/legc_headers.txt | tr -d '\r' | awk '{print $2}' || echo "None")
legc_bytes=$(wc -c < /tmp/legc_body.txt | tr -d ' ' || echo "0")

echo "Content-Encoding: ${legc_encoding}" | tee -a "$REPORT_FILE"
echo "ByteTaper Candidate Header: ${legc_candidate}" | tee -a "$REPORT_FILE"
echo "ByteTaper Skip Reason Header: ${legc_reason}" | tee -a "$REPORT_FILE"
echo "Response Payload Size (Uncompressed): ${legc_bytes} bytes" | tee -a "$REPORT_FILE"

# Assertions
if [[ "${legc_encoding}" == *"gzip"* ]]; then
    echo "ERROR: Expected Leg C response NOT to be gzip compressed! Got Content-Encoding: '${legc_encoding}'"
    exit 1
fi
if [ "${legc_candidate}" != "false" ]; then
    echo "ERROR: Expected Leg C to NOT be a compression candidate! Got: '${legc_candidate}'"
    exit 1
fi
if [ "${legc_reason}" != "below_minimum" ]; then
    echo "ERROR: Expected skip reason 'below_minimum' in Leg C, got '${legc_reason}'"
    exit 1
fi

echo "" | tee -a "$REPORT_FILE"
echo "Running wrk load test on Leg C (Small Response - Not Candidate)..."
WRK_LEGC_OUT=$(mktemp)
wrk -t2 -c10 -d10s -H "Accept-Encoding: gzip" -s benchmarks/lib/latency_reporter.lua --latency "${LEGC_URL}" | tee "$WRK_LEGC_OUT"
cat "$WRK_LEGC_OUT" >> "$REPORT_FILE"

# Extract JSON latency and throughput metrics for Leg C
echo "Extracting JSON latency metrics for Leg C..."
JSON_LEGC_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_LEGC_OUT")
echo "Extracting JSON throughput metrics for Leg C..."
JSON_LEGC_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_LEGC_OUT")

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

# Parse Leg C wrk results
legc_total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_LEGC_OUT" | awk '{print $1}' || echo "0")
legc_non_2xx=$(grep -E "Non-2xx or 3xx responses:" "$WRK_LEGC_OUT" | awk '{print $5}' || echo "0")
if [ -z "$legc_non_2xx" ]; then legc_non_2xx=0; fi
legc_success=$((legc_total_reqs - legc_non_2xx))
legc_transfer=$(grep -E '^[[:space:]]*Transfer/sec:' "$WRK_LEGC_OUT" | awk '{print $2 " " $3}' || echo "N/A")

# Extract container stats for Leg A, Leg B, and Leg C
echo "Extracting container stats..."
JSON_LEGA_STATS=$(./benchmarks/lib/container_stats.sh all)
JSON_LEGB_STATS=$(./benchmarks/lib/container_stats.sh all)
JSON_LEGC_STATS=$(./benchmarks/lib/container_stats.sh all)

# Extract response payload savings
echo "Extracting payload savings..."
uncomp_size=$(curl -s -o /dev/null -w "%{size_download}" "${TARGET_HOST}/large-json" || echo "0")
JSON_LEGA_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$uncomp_size" "$lega_bytes")
JSON_LEGB_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$uncomp_size" "$legb_bytes")
JSON_LEGC_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$legc_bytes" "$legc_bytes")

echo "" >> "$REPORT_FILE"
echo "=== Parsed Scenario Metrics ===" >> "$REPORT_FILE"
{
    echo "Leg A (Envoy Compression Only) - Payload Size: ${lega_bytes} bytes"
    echo "Leg A Content-Encoding: ${lega_encoding}"
    echo "Leg A Total Requests (10s): ${lega_total_reqs}"
    echo "Leg A Successful Requests: ${lega_success}"
    echo "Leg A Transfer Rate: ${lega_transfer}"
    echo "Leg A Latency JSON: ${JSON_LEGA_LATENCY}"
    echo "Leg A Throughput JSON: ${JSON_LEGA_THROUGHPUT}"
    echo "Leg A Container Stats JSON: ${JSON_LEGA_STATS}"
    echo "Leg A Payload Savings JSON: ${JSON_LEGA_SAVINGS}"
    echo ""
    echo "Leg B (Envoy + ByteTaper Decision) - Payload Size: ${legb_bytes} bytes"
    echo "Leg B Content-Encoding: ${legb_encoding}"
    echo "Leg B Candidate: ${legb_candidate}"
    echo "Leg B Total Requests (10s): ${legb_total_reqs}"
    echo "Leg B Successful Requests: ${legb_success}"
    echo "Leg B Transfer Rate: ${legb_transfer}"
    echo "Leg B Latency JSON: ${JSON_LEGB_LATENCY}"
    echo "Leg B Throughput JSON: ${JSON_LEGB_THROUGHPUT}"
    echo "Leg B Container Stats JSON: ${JSON_LEGB_STATS}"
    echo "Leg B Payload Savings JSON: ${JSON_LEGB_SAVINGS}"
    echo ""
    echo "Leg C (Small Response - Not Candidate) - Payload Size: ${legc_bytes} bytes"
    echo "Leg C Content-Encoding: ${legc_encoding}"
    echo "Leg C Candidate: ${legc_candidate}"
    echo "Leg C Decision Reason: ${legc_reason}"
    echo "Leg C Total Requests (10s): ${legc_total_reqs}"
    echo "Leg C Successful Requests: ${legc_success}"
    echo "Leg C Transfer Rate: ${legc_transfer}"
    echo "Leg C Latency JSON: ${JSON_LEGC_LATENCY}"
    echo "Leg C Throughput JSON: ${JSON_LEGC_THROUGHPUT}"
    echo "Leg C Container Stats JSON: ${JSON_LEGC_STATS}"
    echo "Leg C Payload Savings JSON: ${JSON_LEGC_SAVINGS}"
} >> "$REPORT_FILE"

# Cleanup
rm -f "$WRK_LEGA_OUT" "$WRK_LEGB_OUT" "$WRK_LEGC_OUT" /tmp/lega_headers.txt /tmp/lega_body.txt /tmp/legb_headers.txt /tmp/legb_body.txt /tmp/legc_headers.txt /tmp/legc_body.txt

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
cat "$REPORT_FILE" | tail -n 40

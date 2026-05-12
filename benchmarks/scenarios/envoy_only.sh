#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Configuration
SCENARIO="envoy_only"
TARGET_URL="${ENVOY_BASELINE_URL:-http://envoy-baseline:10000}/api/v1/cached/fast/bench"
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

echo "Target is UP. Starting wrk..."

echo "Config Gauges JSON: {}" >> "$REPORT_FILE"

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
total_reqs=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_OUT" | awk '{print $1}')
if [ -z "$total_reqs" ]; then
    total_reqs=0
fi

# Non-2xx/3xx if present
non_2xx_3xx=$(grep -E "Non-2xx or 3xx responses:" "$WRK_OUT" | awk '{print $5}')
if [ -z "$non_2xx_3xx" ]; then
    non_2xx_3xx=0
fi

# Success count
success_count=$((total_reqs - non_2xx_3xx))

# Bytes read & transfer rate
bytes_read=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_OUT" | awk -F', ' '{print $2}')
transfer_rate=$(grep -E "^Transfer/sec:" "$WRK_OUT" | awk '{print $2}')

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
cat "$REPORT_FILE" | tail -n 14

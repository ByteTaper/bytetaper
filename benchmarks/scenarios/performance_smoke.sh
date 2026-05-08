#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Configuration
SCENARIO="performance_smoke"
TARGET_HOST="${ENVOY_OBSERVE_URL:-http://envoy-bytetaper-observe:10000}"
REPORT_DIR="reports/benchmarks"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${REPORT_DIR}/benchmark_results_${TIMESTAMP}_${SCENARIO}.txt"

mkdir -p "$REPORT_DIR"

# Collect system info
{
    echo "=== ByteTaper Benchmark Execution ==="
    echo "Scenario: $SCENARIO"
    echo "Time: $(date)"
    echo "Target: $TARGET_HOST/api/v1/cached/fast/bench"
    echo ""
    echo "=== System Information ==="
    echo "OS: $(uname -snrmo)"
    echo "CPU cores: $(nproc)"
    echo "Memory Total: $(grep MemTotal /proc/meminfo | awk '{print $2/1024 " MB"}' || echo 'N/A')"
    echo ""
} > "$REPORT_FILE"

cat "$REPORT_FILE"

# Check target availability
echo "Checking target availability..."
if ! curl -s --fail "http://envoy-baseline:10000/api/v1/cached/fast/bench" > /dev/null; then
    echo "ERROR: Target endpoint http://envoy-baseline:10000/api/v1/cached/fast/bench is unavailable."
    echo "Status: Baseline UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi
if ! curl -s --fail "${TARGET_HOST}/api/v1/cached/fast/bench" > /dev/null; then
    echo "ERROR: Target endpoint ${TARGET_HOST}/api/v1/cached/fast/bench is unavailable."
    echo "Status: Observe UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi
echo "Targets are UP."

# Response equivalence verification
echo "Comparing response body between baseline and observe targets..."
baseline_body=$(curl -s "http://envoy-baseline:10000/api/v1/cached/fast/bench")
observe_body=$(curl -s "${TARGET_HOST}/api/v1/cached/fast/bench")

if [ "$baseline_body" != "$observe_body" ]; then
    echo "ERROR: Response bodies do not match." >&2
    exit 1
fi
echo "Response body equivalence verified successfully (100% identical)."

# Executing baseline load test
echo "Starting rapid 3-second wrk smoke load test on baseline target..."
WRK_BASELINE_OUT=$(mktemp)
wrk -t2 -c10 -d3s -s benchmarks/lib/latency_reporter.lua --latency "http://envoy-baseline:10000/api/v1/cached/fast/bench" > "$WRK_BASELINE_OUT"
cat "$WRK_BASELINE_OUT"

JSON_BASELINE_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_BASELINE_OUT")
JSON_BASELINE_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_BASELINE_OUT")
JSON_BASELINE_STATS=$(./benchmarks/lib/container_stats.sh all)

# Executing observe load test
echo "Starting rapid 3-second wrk smoke load test on observe target..."
WRK_OBSERVE_OUT=$(mktemp)
wrk -t2 -c10 -d3s -s benchmarks/lib/latency_reporter.lua --latency "${TARGET_HOST}/api/v1/cached/fast/bench" > "$WRK_OBSERVE_OUT"
cat "$WRK_OBSERVE_OUT"

JSON_OBSERVE_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_OBSERVE_OUT")
JSON_OBSERVE_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_OBSERVE_OUT")
JSON_OBSERVE_STATS=$(./benchmarks/lib/container_stats.sh all)

# Calculate payload savings
original_size=$(echo -n "$baseline_body" | wc -c)
optimized_size=$(echo -n "$observe_body" | wc -c)
JSON_BASELINE_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$original_size" "$original_size")
JSON_OBSERVE_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$original_size" "$optimized_size")

# Append metrics block to human-readable text report
{
    echo "=== Parsed Scenario Metrics ==="
    echo "Body Equivalence Match: YES"
    echo "Baseline Latency JSON: $JSON_BASELINE_LATENCY"
    echo "Baseline Throughput JSON: $JSON_BASELINE_THROUGHPUT"
    echo "Baseline Container Stats JSON: $JSON_BASELINE_STATS"
    echo "Baseline Payload Savings JSON: $JSON_BASELINE_SAVINGS"
    echo "Observe Latency JSON: $JSON_OBSERVE_LATENCY"
    echo "Observe Throughput JSON: $JSON_OBSERVE_THROUGHPUT"
    echo "Observe Container Stats JSON: $JSON_OBSERVE_STATS"
    echo "Observe Payload Savings JSON: $JSON_OBSERVE_SAVINGS"
} >> "$REPORT_FILE"

# Cleanup
rm -f "$WRK_BASELINE_OUT" "$WRK_OBSERVE_OUT"

# Validate report fields correctness
./benchmarks/report/validate.sh "$REPORT_FILE"

# Compile structured JSON report
./benchmarks/report/generate_json_report.sh "$REPORT_FILE"

# Compile stylized executive Markdown report
./benchmarks/report/generate_markdown_report.sh "${REPORT_FILE%.txt}.json"

# Validate performance regression thresholds
./benchmarks/report/check_thresholds.sh "${REPORT_FILE%.txt}.json"

echo ""
echo "Smoke benchmark complete successfully."
echo "Results saved to: $REPORT_FILE"
cat "$REPORT_FILE" | tail -n 15

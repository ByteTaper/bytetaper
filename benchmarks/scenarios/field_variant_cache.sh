#!/usr/bin/env bash
// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Configuration
SCENARIO="field_variant_cache"
TARGET_HOST="${ENVOY_FIELD_FILTERING_URL:-http://envoy-bytetaper-field-filtering:10000}"
MOCK_API="${MOCK_API_URL:-http://mock-api:8080}"
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
if ! curl -s --fail "${TARGET_HOST}/medium-json?fields=id,name" > /dev/null; then
    echo "ERROR: Target endpoint ${TARGET_HOST}/medium-json is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi

echo "Targets are UP."

# --------------------------------------------------
# Leg A: Medium JSON with fields=id,name
# --------------------------------------------------
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg A: Medium JSON field selection (?fields=id,name)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LEG_A_URL="${TARGET_HOST}/medium-json?fields=id,name"

# 1. First Request: Cache Miss (Admission Count = 1)
calls_before=$(curl -s "${MOCK_API}/call-count" || echo "0")
curl -sD /tmp/lega_1.txt "${LEG_A_URL}" > /dev/null
calls_after=$(curl -s "${MOCK_API}/call-count" || echo "0")
calls_delta_1=$((calls_after - calls_before))

header_cached_1=$(grep -i 'x-bytetaper-cached-response' /tmp/lega_1.txt | tr -d '\r' | awk '{print $2}' || echo "false")
header_layer_1=$(grep -i 'x-bytetaper-cache-layer' /tmp/lega_1.txt | tr -d '\r' | awk '{print $2}' || echo "None")

echo "Request 1 (Miss):" | tee -a "$REPORT_FILE"
echo "  Cached-Response Header: ${header_cached_1}" | tee -a "$REPORT_FILE"
echo "  Cache-Layer Header: ${header_layer_1}" | tee -a "$REPORT_FILE"
echo "  Upstream Calls Delta: ${calls_delta_1}" | tee -a "$REPORT_FILE"

# 2. Second Request: Cache Miss (Admission Count = 2, Admitted + Stored)
calls_before=$(curl -s "${MOCK_API}/call-count" || echo "0")
curl -sD /tmp/lega_2.txt "${LEG_A_URL}" > /dev/null
calls_after=$(curl -s "${MOCK_API}/call-count" || echo "0")
calls_delta_2=$((calls_after - calls_before))

header_cached_2=$(grep -i 'x-bytetaper-cached-response' /tmp/lega_2.txt | tr -d '\r' | awk '{print $2}' || echo "false")
header_layer_2=$(grep -i 'x-bytetaper-cache-layer' /tmp/lega_2.txt | tr -d '\r' | awk '{print $2}' || echo "None")

echo "Request 2 (Miss, Met Threshold & Admitted):" | tee -a "$REPORT_FILE"
echo "  Cached-Response Header: ${header_cached_2}" | tee -a "$REPORT_FILE"
echo "  Cache-Layer Header: ${header_layer_2}" | tee -a "$REPORT_FILE"
echo "  Upstream Calls Delta: ${calls_delta_2}" | tee -a "$REPORT_FILE"

# 3. Third Request: Cache Hit (L1 Variant)
calls_before=$(curl -s "${MOCK_API}/call-count" || echo "0")
curl -sD /tmp/lega_3.txt "${LEG_A_URL}" > /dev/null
calls_after=$(curl -s "${MOCK_API}/call-count" || echo "0")
calls_delta_3=$((calls_after - calls_before))

header_cached_3=$(grep -i 'x-bytetaper-cached-response' /tmp/lega_3.txt | tr -d '\r' | awk '{print $2}' || echo "false")
header_layer_3=$(grep -i 'x-bytetaper-cache-layer' /tmp/lega_3.txt | tr -d '\r' | awk '{print $2}' || echo "None")

echo "Request 3 (L1 Variant Cache Hit):" | tee -a "$REPORT_FILE"
echo "  Cached-Response Header: ${header_cached_3}" | tee -a "$REPORT_FILE"
echo "  Cache-Layer Header: ${header_layer_3}" | tee -a "$REPORT_FILE"
echo "  Upstream Calls Delta: ${calls_delta_3}" | tee -a "$REPORT_FILE"

# Assert correctness
if [ "${header_cached_3}" != "true" ] || [ "${header_layer_3}" != "L1_VAR" ]; then
    echo "ERROR: Expected L1_VAR cache hit on Request 3, got layer=${header_layer_3} cached=${header_cached_3}"
    exit 1
fi
if [ "${calls_delta_3}" -ne 0 ]; then
    echo "ERROR: Upstream call triggered on cache hit!"
    exit 1
fi

echo "Running wrk load test on Leg A (warm variant cache)..."
WRK_A_OUT=$(mktemp)
wrk -t2 -c10 -d5s -s benchmarks/lib/latency_reporter.lua --latency "${LEG_A_URL}" | tee "$WRK_A_OUT"
cat "$WRK_A_OUT" >> "$REPORT_FILE"

JSON_A_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_A_OUT")
JSON_A_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_A_OUT")


# --------------------------------------------------
# Leg B: Medium JSON with fields=id,price
# --------------------------------------------------
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
echo "Leg B: Medium JSON field selection (?fields=id,price)" | tee -a "$REPORT_FILE"
echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

LEG_B_URL="${TARGET_HOST}/medium-json?fields=id,price"

# 1. Warm up Leg B to admit and cache the variant
echo "Warming up Leg B..." | tee -a "$REPORT_FILE"
curl -s "${LEG_B_URL}" > /dev/null # Req 1 (Miss, count=1)
curl -s "${LEG_B_URL}" > /dev/null # Req 2 (Miss, count=2 -> Admitted)

calls_before=$(curl -s "${MOCK_API}/call-count" || echo "0")
curl -sD /tmp/legb_3.txt "${LEG_B_URL}" > /dev/null # Req 3 (Hit)
calls_after=$(curl -s "${MOCK_API}/call-count" || echo "0")
calls_delta_b=$((calls_after - calls_before))

header_cached_b=$(grep -i 'x-bytetaper-cached-response' /tmp/legb_3.txt | tr -d '\r' | awk '{print $2}' || echo "false")
header_layer_b=$(grep -i 'x-bytetaper-cache-layer' /tmp/legb_3.txt | tr -d '\r' | awk '{print $2}' || echo "None")

echo "Request 3 for Leg B:" | tee -a "$REPORT_FILE"
echo "  Cached-Response Header: ${header_cached_b}" | tee -a "$REPORT_FILE"
echo "  Cache-Layer Header: ${header_layer_b}" | tee -a "$REPORT_FILE"
echo "  Upstream Calls Delta: ${calls_delta_b}" | tee -a "$REPORT_FILE"

if [ "${header_cached_b}" != "true" ] || [ "${header_layer_b}" != "L1_VAR" ]; then
    echo "ERROR: Expected L1_VAR cache hit on Leg B Request 3!"
    exit 1
fi

echo "Running wrk load test on Leg B (warm variant cache)..."
WRK_B_OUT=$(mktemp)
wrk -t2 -c10 -d5s -s benchmarks/lib/latency_reporter.lua --latency "${LEG_B_URL}" | tee "$WRK_B_OUT"
cat "$WRK_B_OUT" >> "$REPORT_FILE"

JSON_B_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_B_OUT")
JSON_B_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_B_OUT")


# --------------------------------------------------
# Section 3: Summary and Report Compilation
# --------------------------------------------------
# Parse wrk counts
total_reqs_a=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_A_OUT" | awk '{print $1}' || echo "0")
total_reqs_b=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_B_OUT" | awk '{print $1}' || echo "0")

JSON_A_STATS=$(./benchmarks/lib/container_stats.sh all)
JSON_B_STATS=$(./benchmarks/lib/container_stats.sh all)

med_orig=$(curl -s -o /dev/null -w "%{size_download}" "${TARGET_HOST}/medium-json" || echo "100000")
med_opt=$(curl -s -o /dev/null -w "%{size_download}" "${LEG_A_URL}" || echo "1000")

JSON_A_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$med_orig" "$med_opt")
JSON_B_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$med_orig" "$med_opt")

echo "" >> "$REPORT_FILE"
echo "=== Parsed Scenario Metrics ===" >> "$REPORT_FILE"
{
    echo "Leg A (fields=id,name) Requests: ${total_reqs_a}"
    echo "Leg A Success Count: ${total_reqs_a}"
    echo "Leg A Bytes Saved: $((med_orig - med_opt)) bytes"
    echo "Leg A Reduction Ratio: $(awk "BEGIN {printf \"%.2f%%\", (($med_orig - $med_opt) / $med_orig) * 100}")"
    echo "Leg A Latency JSON: ${JSON_A_LATENCY}"
    echo "Leg A Throughput JSON: ${JSON_A_THROUGHPUT}"
    echo "Leg A Container Stats JSON: ${JSON_A_STATS}"
    echo "Leg A Payload Savings JSON: ${JSON_A_SAVINGS}"
    echo ""
    echo "Leg B (fields=id,price) Requests: ${total_reqs_b}"
    echo "Leg B Success Count: ${total_reqs_b}"
    echo "Leg B Bytes Saved: $((med_orig - med_opt)) bytes"
    echo "Leg B Reduction Ratio: $(awk "BEGIN {printf \"%.2f%%\", (($med_orig - $med_opt) / $med_orig) * 100}")"
    echo "Leg B Latency JSON: ${JSON_B_LATENCY}"
    echo "Leg B Throughput JSON: ${JSON_B_THROUGHPUT}"
    echo "Leg B Container Stats JSON: ${JSON_B_STATS}"
    echo "Leg B Payload Savings JSON: ${JSON_B_SAVINGS}"
} >> "$REPORT_FILE"

# Cleanup
rm -f "$WRK_A_OUT" "$WRK_B_OUT" /tmp/lega_1.txt /tmp/lega_2.txt /tmp/lega_3.txt /tmp/legb_3.txt

# Validate report integrity
./benchmarks/report/validate.sh "$REPORT_FILE"

# Compile JSON report
./benchmarks/report/generate_json_report.sh "$REPORT_FILE"

# Compile Markdown report
./benchmarks/report/generate_markdown_report.sh "${REPORT_FILE%.txt}.json"

# Check thresholds
./benchmarks/report/check_thresholds.sh "${REPORT_FILE%.txt}.json"

echo ""
echo "Benchmark scenario field_variant_cache completed successfully."
echo "Results saved to: $REPORT_FILE"

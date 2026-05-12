#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

SCENARIO="header_variance_cache"
TARGET_HOST="${ENVOY_HEADER_VARIANCE_URL:-http://envoy-bytetaper-header-variance:10000}"
MOCK_API="${MOCK_API_URL:-http://mock-api:8080}"
REPORT_DIR="reports/benchmarks"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${REPORT_DIR}/benchmark_results_${TIMESTAMP}_${SCENARIO}.txt"

mkdir -p "$REPORT_DIR"

# Healthcheck
if ! curl -s --fail "${TARGET_HOST}/small-json" > /dev/null; then
    echo "ERROR: Target ${TARGET_HOST}/small-json is unavailable."
    exit 1
fi
echo "Targets are UP."

echo "Capturing startup configuration gauges..."
METRICS_HOST="${METRICS_HEADER_VARIANCE_URL:-http://bytetaper-extproc-header-variance:18081}"
metrics_raw=$(curl -s "${METRICS_HOST}/metrics" || echo "")
worker_count=$(echo "$metrics_raw" | grep "bytetaper_worker_count_effective" | grep -v "#" | awk '{print $2}' || echo "0")
lookup_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_lookup_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
store_quota=$(echo "$metrics_raw" | grep "bytetaper_worker_store_lane_quota_effective" | grep -v "#" | awk '{print $2}' || echo "0")
async_store_max_body=$(echo "$metrics_raw" | grep "bytetaper_worker_async_store_max_body_size_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_block_cache_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_block_cache_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_write_buffer_mb=$(echo "$metrics_raw" | grep "bytetaper_l2_write_buffer_mb_effective" | grep -v "#" | awk '{print $2}' || echo "0")
l2_max_background_jobs=$(echo "$metrics_raw" | grep "bytetaper_l2_max_background_jobs_effective" | grep -v "#" | awk '{print $2}' || echo "0")

echo "Config Gauges JSON: {\"worker_count\": ${worker_count:-0}, \"lookup_quota\": ${lookup_quota:-0}, \"store_quota\": ${store_quota:-0}, \"async_store_max_body\": ${async_store_max_body:-0}, \"l2_block_cache_mb\": ${l2_block_cache_mb:-0}, \"l2_write_buffer_mb\": ${l2_write_buffer_mb:-0}, \"l2_max_background_jobs\": ${l2_max_background_jobs:-0}}" >> "$REPORT_FILE"

# Shared bust param — same for both legs so differ only by Accept-Language
BASE_URL="${TARGET_HOST}/small-json?_b=${TIMESTAMP}"

# --------------------------------------------------
# Leg A: Accept-Language: id-ID
# --------------------------------------------------
echo "Leg A: Accept-Language: id-ID"

# Warm: req 1 → store in L1
curl -s -H "Accept-Language: id-ID" "${BASE_URL}" > /dev/null

# Req 2 → L1 hit
calls_before=$(curl -s "${MOCK_API}/call-count")
curl -sD /tmp/lega_hit.txt -H "Accept-Language: id-ID" "${BASE_URL}" > /dev/null
calls_after=$(curl -s "${MOCK_API}/call-count")
lega_delta=$((calls_after - calls_before))

lega_cached=$(grep -i 'x-bytetaper-cached-response' /tmp/lega_hit.txt | tr -d '\r' | awk '{print $2}' || echo "false")
lega_layer=$(grep -i 'x-bytetaper-cache-layer' /tmp/lega_hit.txt | tr -d '\r' | awk '{print $2}' || echo "None")

echo "  Cached: ${lega_cached}  Layer: ${lega_layer}  Delta: ${lega_delta}"

if [ "${lega_cached}" != "true" ] || [ "${lega_layer}" != "L1" ]; then
    echo "ERROR: Expected L1 hit for id-ID on req 2, got cached=${lega_cached} layer=${lega_layer}"
    exit 1
fi
if [ "${lega_delta}" -ne 0 ]; then
    echo "ERROR: Upstream call triggered on L1 hit for id-ID!"
    exit 1
fi

# --------------------------------------------------
# Isolation assertion: en-US must MISS when id-ID is warm
# --------------------------------------------------
echo "Isolation check: en-US must miss when id-ID is warm"

calls_before=$(curl -s "${MOCK_API}/call-count")
curl -sD /tmp/iso_check.txt -H "Accept-Language: en-US" "${BASE_URL}" > /dev/null
calls_after=$(curl -s "${MOCK_API}/call-count")
iso_delta=$((calls_after - calls_before))

iso_cached=$(grep -i 'x-bytetaper-cached-response' /tmp/iso_check.txt | tr -d '\r' | awk '{print $2}' || echo "false")

if [ "${iso_delta}" -eq 0 ] || [ "${iso_cached}" = "true" ]; then
    echo "ERROR: Accept-Language isolation failed! en-US was served from id-ID cache entry."
    exit 1
fi
echo "  PASS: isolation confirmed (en-US produced cache miss, delta=${iso_delta})"

# --------------------------------------------------
# Leg B: Accept-Language: en-US — warm and measure
# --------------------------------------------------
echo "Leg B: Accept-Language: en-US"

# Req 2 → L1 hit (first req was the isolation probe above)
calls_before=$(curl -s "${MOCK_API}/call-count")
curl -sD /tmp/legb_hit.txt -H "Accept-Language: en-US" "${BASE_URL}" > /dev/null
calls_after=$(curl -s "${MOCK_API}/call-count")
legb_delta=$((calls_after - calls_before))

legb_cached=$(grep -i 'x-bytetaper-cached-response' /tmp/legb_hit.txt | tr -d '\r' | awk '{print $2}' || echo "false")
legb_layer=$(grep -i 'x-bytetaper-cache-layer' /tmp/legb_hit.txt | tr -d '\r' | awk '{print $2}' || echo "None")

echo "  Cached: ${legb_cached}  Layer: ${legb_layer}  Delta: ${legb_delta}"

if [ "${legb_cached}" != "true" ] || [ "${legb_layer}" != "L1" ]; then
    echo "ERROR: Expected L1 hit for en-US on req 2, got cached=${legb_cached} layer=${legb_layer}"
    exit 1
fi
if [ "${legb_delta}" -ne 0 ]; then
    echo "ERROR: Upstream call triggered on L1 hit for en-US!"
    exit 1
fi

# --------------------------------------------------
# wrk load tests
# --------------------------------------------------
echo "Running wrk load test on Leg A (id-ID, warm L1)..."
WRK_A_OUT=$(mktemp)
wrk -t2 -c10 -d5s -H "Accept-Language: id-ID" \
    -s benchmarks/lib/latency_reporter.lua --latency "${BASE_URL}" | tee "$WRK_A_OUT"

JSON_A_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_A_OUT")
JSON_A_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_A_OUT")

echo "Running wrk load test on Leg B (en-US, warm L1)..."
WRK_B_OUT=$(mktemp)
wrk -t2 -c10 -d5s -H "Accept-Language: en-US" \
    -s benchmarks/lib/latency_reporter.lua --latency "${BASE_URL}" | tee "$WRK_B_OUT"

JSON_B_LATENCY=$(./benchmarks/lib/latency_parser.sh "$WRK_B_OUT")
JSON_B_THROUGHPUT=$(./benchmarks/lib/throughput_parser.sh "$WRK_B_OUT")

# --------------------------------------------------
# Report compilation (follows field_variant_cache.sh pattern)
# --------------------------------------------------
total_reqs_a=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_A_OUT" | awk '{print $1}' || echo "0")
total_reqs_b=$(grep -E '^[[:space:]]*[0-9]+ requests in' "$WRK_B_OUT" | awk '{print $1}' || echo "0")

JSON_A_STATS=$(./benchmarks/lib/container_stats.sh all)
JSON_B_STATS=$(./benchmarks/lib/container_stats.sh all)

orig_size=$(curl -s -o /dev/null -w "%{size_download}" "${TARGET_HOST}/small-json" || echo "0")
opt_size_a=$(curl -sH "Accept-Language: id-ID" -o /dev/null -w "%{size_download}" "${BASE_URL}" || echo "0")
opt_size_b=$(curl -sH "Accept-Language: en-US" -o /dev/null -w "%{size_download}" "${BASE_URL}" || echo "0")

JSON_A_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$orig_size" "$opt_size_a")
JSON_B_SAVINGS=$(./benchmarks/lib/payload_savings_parser.sh "$orig_size" "$opt_size_b")

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
    echo "=== Parsed Scenario Metrics ==="
    echo "Leg A (Accept-Language: id-ID) Requests: ${total_reqs_a}"
    echo "Leg A Success Count: ${total_reqs_a}"
    echo "Leg A Latency JSON: ${JSON_A_LATENCY}"
    echo "Leg A Throughput JSON: ${JSON_A_THROUGHPUT}"
    echo "Leg A Container Stats JSON: ${JSON_A_STATS}"
    echo "Leg A Payload Savings JSON: ${JSON_A_SAVINGS}"
    echo ""
    echo "Leg B (Accept-Language: en-US) Requests: ${total_reqs_b}"
    echo "Leg B Success Count: ${total_reqs_b}"
    echo "Leg B Latency JSON: ${JSON_B_LATENCY}"
    echo "Leg B Throughput JSON: ${JSON_B_THROUGHPUT}"
    echo "Leg B Container Stats JSON: ${JSON_B_STATS}"
    echo "Leg B Payload Savings JSON: ${JSON_B_SAVINGS}"
} > "$REPORT_FILE"

# Cleanup
rm -f "$WRK_A_OUT" "$WRK_B_OUT" /tmp/lega_hit.txt /tmp/legb_hit.txt /tmp/iso_check.txt

./benchmarks/report/validate.sh "$REPORT_FILE"
./benchmarks/report/generate_json_report.sh "$REPORT_FILE"
./benchmarks/report/generate_markdown_report.sh "${REPORT_FILE%.txt}.json"
./benchmarks/report/check_thresholds.sh "${REPORT_FILE%.txt}.json"

echo ""
echo "Benchmark scenario ${SCENARIO} completed successfully."
echo "Results saved to: $REPORT_FILE"

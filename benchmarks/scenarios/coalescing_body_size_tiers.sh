#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

SCENARIO="coalescing_body_size_tiers"
ENVOY_HOST="${ENVOY_COALESCING_URL:-http://envoy-bytetaper-coalescing:10000}"
METRICS_HOST="${METRICS_COALESCING_URL:-http://bytetaper-extproc-coalescing:18081}"
MOCK_HOST="${MOCK_URL:-http://mock-api:8080}"
REPORT_DIR="reports/benchmarks"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${REPORT_DIR}/benchmark_results_${TIMESTAMP}_${SCENARIO}.txt"
N="${BYTETAPER_COALESCING_BURST_N:-50}"

source ./benchmarks/lib/mock_api_metrics.sh
source ./benchmarks/lib/coalescing_metrics_helpers.sh

mkdir -p "$REPORT_DIR"

{
    echo "=== ByteTaper Benchmark Execution ==="
    echo "Scenario: $SCENARIO"
    echo "Time: $(date)"
    echo "Target Envoy: $ENVOY_HOST"
    echo "Metrics Host: $METRICS_HOST"
    echo "Mock API Host: $MOCK_HOST"
    echo ""
    echo "=== System Information ==="
    echo "OS: $(uname -snrmo)"
    echo "CPU cores: $(nproc)"
    echo "Memory Total: $(grep MemTotal /proc/meminfo | awk '{print $2/1024 " MB"}' || echo 'N/A')"
    echo ""
} > "$REPORT_FILE"

cat "$REPORT_FILE"

echo "Checking target availability..."
if ! curl -s --fail "${ENVOY_HOST}/products/fast/healthcheck-target-availability" > /dev/null; then
    echo "ERROR: Target Envoy at ${ENVOY_HOST} is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi
if ! curl -s --fail "${METRICS_HOST}/metrics" > /dev/null; then
    echo "ERROR: Metrics server at ${METRICS_HOST} is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi
if ! curl -s --fail "${MOCK_HOST}/metrics.json" > /dev/null; then
    echo "ERROR: Mock API metrics at ${MOCK_HOST}/metrics.json is unavailable."
    echo "Status: UNAVAILABLE" >> "$REPORT_FILE"
    exit 1
fi

echo "All targets are UP."

run_tier_leg() {
    local leg_name=$1
    local url=$2
    local expect_l1_hit=$3      # "yes"/"no"
    local expect_probe_hit=$4   # "yes"/"no"
    local expect_fallback=$5    # "yes"/"no"

    echo "--------------------------------------------------" | tee -a "$REPORT_FILE"
    echo "$leg_name" | tee -a "$REPORT_FILE"
    echo "--------------------------------------------------" | tee -a "$REPORT_FILE"

    local before_metrics
    local after_metrics
    local mock_metrics
    local mock_calls
    local coal_json
    local wrk_out
    local latency_json
    local throughput_json
    local stats_json
    local savings_json
    local body_size

    before_metrics=$(snapshot_coalescing_metrics)
    mock_api_reset_metrics "$MOCK_HOST"

    # Fast delay-kind to ensure overlap: self.path with "/fast/" delays mock-api response by 20ms
    python3 -c "
import threading, urllib.request
def req():
    try: urllib.request.urlopen('${url}').read()
    except Exception as e: pass

threads = [threading.Thread(target=req) for _ in range($N)]
for t in threads: t.start()
for t in threads: t.join()
"

    echo "Concurrent burst complete."
    after_metrics=$(snapshot_coalescing_metrics)
    mock_metrics=$(mock_api_snapshot "$MOCK_HOST")
    mock_calls=$(mock_api_upstream_requests "$mock_metrics")
    coal_json=$(coalescing_delta "$before_metrics" "$after_metrics" "$N" "$mock_calls")

    echo "Running wrk latency check for $leg_name..."
    wrk_out=$(mktemp)
    wrk -t2 -c5 -d3s -s benchmarks/lib/latency_reporter.lua --latency "$url" | tee "$wrk_out"
    latency_json=$(./benchmarks/lib/latency_parser.sh "$wrk_out")
    throughput_json=$(./benchmarks/lib/throughput_parser.sh "$wrk_out")
    rm -f "$wrk_out"

    stats_json=$(./benchmarks/lib/container_stats.sh all)
    body_size=$(curl -s -o /dev/null -w "%{size_download}" "$url" || echo "0")
    savings_json=$(./benchmarks/lib/payload_savings_parser.sh "$body_size" "$body_size")

    {
        echo "${leg_name} Mock Upstream Calls: $mock_calls"
        echo "${leg_name} Latency JSON: $latency_json"
        echo "${leg_name} Throughput JSON: $throughput_json"
        echo "${leg_name} Container Stats JSON: $stats_json"
        echo "${leg_name} Payload Savings JSON: $savings_json"
        echo "${leg_name} Coalescing JSON: $coal_json"
        echo "${leg_name} Mock API Metrics JSON: $mock_metrics"
        echo ""
    } | tee -a "$REPORT_FILE"

    local leaders
    local followers
    local l1_hits
    local probe_hits
    local fallbacks
    leaders=$(jq -r '.leaders' <<< "$coal_json")
    followers=$(jq -r '.followers' <<< "$coal_json")
    l1_hits=$(jq -r '.follower_l1_hit' <<< "$coal_json")
    probe_hits=$(jq -r '.probe_hit' <<< "$coal_json")
    fallbacks=$(jq -r '.fallbacks' <<< "$coal_json")

    if [ "$leaders" -eq 0 ]; then
        echo "ERROR: Expected at least 1 leader request for $leg_name, got 0"
        exit 1
    fi
    if [ "$followers" -eq 0 ]; then
        echo "ERROR: Expected followers for overlapping requests in $leg_name, got 0"
        exit 1
    fi
    if [ "$expect_l1_hit" = "yes" ] && [ "$l1_hits" -eq 0 ]; then
        echo "ERROR: Expected L1 hits for $leg_name, got 0"
        exit 1
    fi
    if [ "$expect_probe_hit" = "yes" ] && [ "$probe_hits" -eq 0 ]; then
        echo "ERROR: Expected sync L2 probe hits for $leg_name, got 0"
        exit 1
    fi
    if [ "$expect_fallback" = "yes" ] && [ "$fallbacks" -eq 0 ]; then
        echo "ERROR: Expected follower fallback requests for $leg_name, got 0"
        exit 1
    fi
}

# Leg L1-Inline (small-json, 527 bytes, must hit L1 inline)
run_tier_leg "Leg L1-Inline" \
    "${ENVOY_HOST}/products/tier-l1/fast/burst-${TIMESTAMP}" \
    "yes" "no" "no"

echo "Cooling down socket pools..."
sleep 3

# Leg L2-Completion (medium-json, 8071 bytes, must probe and hit L2)
run_tier_leg "Leg L2-Completion" \
    "${ENVOY_HOST}/products/tier-l2complete/fast/burst-${TIMESTAMP}" \
    "no" "yes" "no"

echo "Cooling down socket pools..."
sleep 3

# Leg L2-Warm-Only (large-json, 127268 bytes, too large for follower handoff so fallback is expected)
run_tier_leg "Leg L2-Warm-Only" \
    "${ENVOY_HOST}/products/tier-l2warmonly/fast/burst-${TIMESTAMP}" \
    "no" "no" "yes"

./benchmarks/report/validate.sh "$REPORT_FILE"
./benchmarks/report/generate_json_report.sh "$REPORT_FILE"
./benchmarks/report/generate_markdown_report.sh "${REPORT_FILE%.txt}.json"
./benchmarks/report/check_thresholds.sh "${REPORT_FILE%.txt}.json"

echo "Scenario $SCENARIO benchmark complete."
echo "Results saved to: $REPORT_FILE"

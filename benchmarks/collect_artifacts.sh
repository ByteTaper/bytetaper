#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

REPORT_DIR="${BENCHMARK_REPORT_DIR:-reports/benchmarks}"

echo "Locating latest benchmark results in $REPORT_DIR..."
latest_json=$(ls -t "$REPORT_DIR"/benchmark_results_*.json 2>/dev/null | head -n 1 || true)
latest_md=$(ls -t "$REPORT_DIR"/benchmark_results_*.md 2>/dev/null | head -n 1 || true)

if [[ -z "$latest_json" || ! -s "$latest_json" ]]; then
    echo "ERROR: Latest JSON report is missing or empty in $REPORT_DIR" >&2
    exit 1
fi

if [[ -z "$latest_md" || ! -s "$latest_md" ]]; then
    echo "ERROR: Latest Markdown report is missing or empty in $REPORT_DIR" >&2
    exit 1
fi

echo "Deriving fixed-name artifacts..."
cp "$latest_json" benchmark-summary.json
cp "$latest_md" benchmark-summary.md

# Derive mock-api-metrics.json
scenario=$(jq -r '.scenario' benchmark-summary.json)
timestamp=$(jq -r '.timestamp' benchmark-summary.json)
jq -n \
  --arg sc "$scenario" \
  --arg ts "$timestamp" \
  --argjson ma "$(jq '.mock_api' benchmark-summary.json)" \
  '{scenario: $sc, timestamp: $ts, mock_api: $ma}' > mock-api-metrics.json

# Derive coalescing-summary.json
jq -n \
  --argjson bj "$(cat benchmark-summary.json)" \
  '{
    scenario: $bj.scenario,
    timestamp: $bj.timestamp,
    config: $bj.config,
    total_requests: (if $bj.throughput == null then 0 else ([$bj.throughput[].total_requests // 0] | add) end),
    non_2xx_count: (if $bj.throughput == null then 0 else ([$bj.throughput[].failed_requests // 0] | add) end),
    requests_per_sec: (if $bj.throughput == null then 0 else ([$bj.throughput[].throughput.requests_per_second // 0] | add) end),
    latency_ms: (if $bj.latency_ms == null then "UNAVAILABLE" else ($bj.latency_ms | map_values(.latency_ms // "UNAVAILABLE")) end),
    legs: ($bj.coalescing // "UNAVAILABLE"),
    totals: {
      leaders: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].leaders // 0] | add) end),
      followers: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].followers // 0] | add) end),
      fallbacks: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].fallbacks // 0] | add) end)
    },
    l2_handoff: {
      pending: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].l2_pending // 0] | add) end),
      ready: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].l2_ready // 0] | add) end),
      failed: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].l2_failed // 0] | add) end),
      delay_ms_avg: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].l2_delay // 0] | max) end)
    },
    worker_queue: {
      dropped: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].w_dropped // 0] | add) end),
      full: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].w_full // 0] | add) end),
      starvation: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].w_starve // 0] | add) end),
      bytes_in_use: (if $bj.coalescing == null or $bj.coalescing == "UNAVAILABLE" then "UNAVAILABLE" else ([$bj.coalescing[].w_bytes // 0] | max) end)
    }
  }' > coalescing-summary.json

echo "Artifacts successfully compiled and collected."

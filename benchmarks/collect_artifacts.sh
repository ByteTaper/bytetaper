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
    total_requests: (($bj.throughput["Leg A"].total_requests // 0) + ($bj.throughput["Leg B"].total_requests // 0)),
    non_2xx_count: (($bj.throughput["Leg A"].failed_requests // 0) + ($bj.throughput["Leg B"].failed_requests // 0)),
    requests_per_sec: (($bj.throughput["Leg A"].throughput.requests_per_second // 0) + ($bj.throughput["Leg B"].throughput.requests_per_second // 0)),
    latency_ms: {
      "Leg A": ($bj.latency_ms["Leg A"].latency_ms // "UNAVAILABLE"),
      "Leg B": ($bj.latency_ms["Leg B"].latency_ms // "UNAVAILABLE")
    },
    legs: ($bj.coalescing // "UNAVAILABLE"),
    totals: {
      leaders: ((($bj.coalescing["Leg A"].leaders // 0) + ($bj.coalescing["Leg B"].leaders // 0)) // "UNAVAILABLE"),
      followers: ((($bj.coalescing["Leg A"].followers // 0) + ($bj.coalescing["Leg B"].followers // 0)) // "UNAVAILABLE"),
      fallbacks: ((($bj.coalescing["Leg A"].fallbacks // 0) + ($bj.coalescing["Leg B"].fallbacks // 0)) // "UNAVAILABLE")
    },
    l2_handoff: {
      pending: ((($bj.coalescing["Leg A"].l2_pending // 0) + ($bj.coalescing["Leg B"].l2_pending // 0)) // "UNAVAILABLE"),
      ready: ((($bj.coalescing["Leg A"].l2_ready // 0) + ($bj.coalescing["Leg B"].l2_ready // 0)) // "UNAVAILABLE"),
      failed: ((($bj.coalescing["Leg A"].l2_failed // 0) + ($bj.coalescing["Leg B"].l2_failed // 0)) // "UNAVAILABLE"),
      delay_ms_avg: ((if (($bj.coalescing["Leg A"].l2_delay // 0) > 0) then ($bj.coalescing["Leg A"].l2_delay // 0) else ($bj.coalescing["Leg B"].l2_delay // 0) end) // "UNAVAILABLE")
    },
    worker_queue: {
      dropped: ((($bj.coalescing["Leg A"].w_dropped // 0) + ($bj.coalescing["Leg B"].w_dropped // 0)) // "UNAVAILABLE"),
      full: ((($bj.coalescing["Leg A"].w_full // 0) + ($bj.coalescing["Leg B"].w_full // 0)) // "UNAVAILABLE"),
      starvation: ((($bj.coalescing["Leg A"].w_starve // 0) + ($bj.coalescing["Leg B"].w_starve // 0)) // "UNAVAILABLE"),
      bytes_in_use: ((if (($bj.coalescing["Leg A"].w_bytes // 0) > 0) then ($bj.coalescing["Leg A"].w_bytes // 0) else ($bj.coalescing["Leg B"].w_bytes // 0) end) // "UNAVAILABLE")
    }
  }' > coalescing-summary.json

echo "Artifacts successfully compiled and collected."

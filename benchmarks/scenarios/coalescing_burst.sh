#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

SCENARIO="coalescing_burst"
ENVOY_HOST="${ENVOY_COALESCING_URL:-http://envoy-bytetaper-coalescing:10000}"
METRICS_HOST="${METRICS_COALESCING_URL:-http://bytetaper-extproc-coalescing:18081}"
MOCK_HOST="${MOCK_URL:-http://mock-api:8080}"
REPORT_DIR="reports/benchmarks"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="${REPORT_DIR}/benchmark_results_${TIMESTAMP}_${SCENARIO}.txt"
N="${BYTETAPER_COALESCING_BURST_N:-50}"

source ./benchmarks/lib/mock_api_metrics.sh

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

get_metric() {
    local name=$1
    local val
    val=$(curl -s "${METRICS_HOST}/metrics" | grep "^${name} " | awk '{print $2}' || echo "0")
    if [ -z "$val" ]; then val=0; fi
    echo "$val"
}

snapshot_coalescing_metrics() {
    local l2_pending
    local l2_ready
    local l2_failed
    local l2_delay
    local w_dropped
    local w_full
    local w_starve
    local w_bytes
    local probe_total
    local probe_hit
    local probe_miss
    local probe_body_too_large
    local probe_l2ready
    local probe_timeout_final
    local probe_latency_avg
    local probe_latency_max

    l2_pending=$(get_metric bytetaper_coalescing_leader_l2_handoff_pending_total)
    l2_ready=$(get_metric bytetaper_coalescing_leader_l2_handoff_ready_total)
    l2_failed=$(get_metric bytetaper_coalescing_leader_l2_handoff_failed_total)
    l2_delay=$(get_metric bytetaper_coalescing_l2_handoff_publish_delay_ms_avg)
    w_dropped=$(get_metric bytetaper_runtime_worker_enqueue_dropped_total)
    w_full=$(get_metric bytetaper_worker_store_body_pool_full_total)
    w_starve=$(get_metric bytetaper_worker_store_lane_starvation_total)
    w_bytes=$(get_metric bytetaper_worker_store_body_pool_bytes_in_use)

    probe_total=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_total)
    probe_hit=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_hit_total)
    probe_miss=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_miss_total)
    probe_body_too_large=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_body_too_large_total)
    probe_l2ready=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_l2ready_total)
    probe_timeout_final=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_timeout_final_total)
    probe_latency_avg=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_latency_ms_avg)
    probe_latency_max=$(get_metric bytetaper_coalescing_follower_sync_l2_probe_latency_ms_max)

    jq -c -n \
      --argjson leader "$(get_metric bytetaper_coalescing_leader_total)" \
      --argjson follower "$(get_metric bytetaper_coalescing_follower_total)" \
      --argjson cache_hit "$(get_metric bytetaper_coalescing_follower_cache_hit_total)" \
      --argjson fallback "$(get_metric bytetaper_coalescing_fallback_total)" \
      --argjson bypass "$(get_metric bytetaper_coalescing_bypass_total)" \
      --argjson follower_shared_response "$(get_metric bytetaper_coalescing_follower_shared_response_total)" \
      --argjson follower_l1_hit "$(get_metric bytetaper_coalescing_follower_l1_hit_total)" \
      --argjson follower_timeout "$(get_metric bytetaper_coalescing_follower_timeout_total)" \
      --argjson follower_missing "$(get_metric bytetaper_coalescing_follower_missing_total)" \
      --argjson follower_stored_but_no_snapshot "$(get_metric bytetaper_coalescing_follower_stored_but_no_snapshot_total)" \
      --argjson follower_not_cacheable "$(get_metric bytetaper_coalescing_follower_not_cacheable_total)" \
      --argjson follower_failed "$(get_metric bytetaper_coalescing_follower_failed_total)" \
      --argjson follower_pool_queue_full "$(get_metric bytetaper_coalescing_follower_pool_queue_full_total)" \
      --argjson follower_unaccounted "$(get_metric bytetaper_coalescing_follower_unaccounted_total)" \
      --argjson leader_l1_store_success "$(get_metric bytetaper_coalescing_leader_l1_store_success_total)" \
      --argjson leader_l1_store_failed "$(get_metric bytetaper_coalescing_leader_l1_store_failed_total)" \
      --argjson follower_l1_ready "$(get_metric bytetaper_coalescing_follower_l1_ready_total)" \
      --argjson follower_l1_ready_but_miss "$(get_metric bytetaper_coalescing_follower_l1_ready_but_miss_total)" \
      --argjson follower_expired "$(get_metric bytetaper_coalescing_follower_expired_total)" \
      --argjson l2_pending "$l2_pending" \
      --argjson l2_ready "$l2_ready" \
      --argjson l2_failed "$l2_failed" \
      --argjson l2_delay "$l2_delay" \
      --argjson w_dropped "$w_dropped" \
      --argjson w_full "$w_full" \
      --argjson w_starve "$w_starve" \
      --argjson w_bytes "$w_bytes" \
      --argjson probe_total "$probe_total" \
      --argjson probe_hit "$probe_hit" \
      --argjson probe_miss "$probe_miss" \
      --argjson probe_body_too_large "$probe_body_too_large" \
      --argjson probe_l2ready "$probe_l2ready" \
      --argjson probe_timeout_final "$probe_timeout_final" \
      --argjson probe_latency_avg "$probe_latency_avg" \
      --argjson probe_latency_max "$probe_latency_max" \
      '{leader:$leader,follower:$follower,cache_hit:$cache_hit,fallback:$fallback,bypass:$bypass,
        follower_shared_response:$follower_shared_response,follower_l1_hit:$follower_l1_hit,
        follower_timeout:$follower_timeout,follower_missing:$follower_missing,
        follower_stored_but_no_snapshot:$follower_stored_but_no_snapshot,
        follower_not_cacheable:$follower_not_cacheable,follower_failed:$follower_failed,
        follower_pool_queue_full:$follower_pool_queue_full,follower_unaccounted:$follower_unaccounted,
        leader_l1_store_success:$leader_l1_store_success,leader_l1_store_failed:$leader_l1_store_failed,
        follower_l1_ready:$follower_l1_ready,follower_l1_ready_but_miss:$follower_l1_ready_but_miss,
        follower_expired:$follower_expired,
        l2_pending:$l2_pending,l2_ready:$l2_ready,l2_failed:$l2_failed,l2_delay:$l2_delay,
        w_dropped:$w_dropped,w_full:$w_full,w_starve:$w_starve,w_bytes:$w_bytes,
        probe_total:$probe_total,probe_hit:$probe_hit,probe_miss:$probe_miss,
        probe_body_too_large:$probe_body_too_large,probe_l2ready:$probe_l2ready,
        probe_timeout_final:$probe_timeout_final,probe_latency_avg:$probe_latency_avg,
        probe_latency_max:$probe_latency_max}'
}

coalescing_delta() {
    local before=$1
    local after=$2
    local client_requests=$3
    local upstream_calls=$4

    jq -c -n \
      --argjson before "$before" \
      --argjson after "$after" \
      --argjson client_requests_sent "$client_requests" \
      --argjson upstream_mock_calls "$upstream_calls" \
      '{
        client_requests_sent: $client_requests_sent,
        upstream_mock_calls: $upstream_mock_calls,
        leaders: (($after.leader // 0) - ($before.leader // 0)),
        followers: (($after.follower // 0) - ($before.follower // 0)),
        follower_cache_hits: (($after.cache_hit // 0) - ($before.cache_hit // 0)),
        fallbacks: (($after.fallback // 0) - ($before.fallback // 0)),
        bypasses: (($after.bypass // 0) - ($before.bypass // 0)),
        coalescing_ratio: (if $client_requests_sent == 0 then 0 else ([1 - ($upstream_mock_calls / $client_requests_sent), 0] | max) end),
        upstream_amplification_ratio: (if $client_requests_sent == 0 then 0 else ($upstream_mock_calls / $client_requests_sent) end),
        follower_shared_response: (($after.follower_shared_response // 0) - ($before.follower_shared_response // 0)),
        follower_l1_hit: (($after.follower_l1_hit // 0) - ($before.follower_l1_hit // 0)),
        follower_timeout: (($after.follower_timeout // 0) - ($before.follower_timeout // 0)),
        follower_missing: (($after.follower_missing // 0) - ($before.follower_missing // 0)),
        follower_stored_but_no_snapshot: (($after.follower_stored_but_no_snapshot // 0) - ($before.follower_stored_but_no_snapshot // 0)),
        follower_not_cacheable: (($after.follower_not_cacheable // 0) - ($before.follower_not_cacheable // 0)),
        follower_failed: (($after.follower_failed // 0) - ($before.follower_failed // 0)),
        follower_pool_queue_full: (($after.follower_pool_queue_full // 0) - ($before.follower_pool_queue_full // 0)),
        follower_unaccounted: (($after.follower_unaccounted // 0) - ($before.follower_unaccounted // 0)),
        leader_l1_store_success: (($after.leader_l1_store_success // 0) - ($before.leader_l1_store_success // 0)),
        leader_l1_store_failed: (($after.leader_l1_store_failed // 0) - ($before.leader_l1_store_failed // 0)),
        follower_l1_ready: (($after.follower_l1_ready // 0) - ($before.follower_l1_ready // 0)),
        follower_l1_ready_but_miss: (($after.follower_l1_ready_but_miss // 0) - ($before.follower_l1_ready_but_miss // 0)),
        follower_expired: (($after.follower_expired // 0) - ($before.follower_expired // 0)),
        l2_pending: (($after.l2_pending // 0) - ($before.l2_pending // 0)),
        l2_ready: (($after.l2_ready // 0) - ($before.l2_ready // 0)),
        l2_failed: (($after.l2_failed // 0) - ($before.l2_failed // 0)),
        l2_delay: ($after.l2_delay // 0),
        w_dropped: (($after.w_dropped // 0) - ($before.w_dropped // 0)),
        w_full: (($after.w_full // 0) - ($before.w_full // 0)),
        w_starve: (($after.w_starve // 0) - ($before.w_starve // 0)),
        w_bytes: ($after.w_bytes // 0),
        probe_total: (($after.probe_total // 0) - ($before.probe_total // 0)),
        probe_hit: (($after.probe_hit // 0) - ($before.probe_hit // 0)),
        probe_miss: (($after.probe_miss // 0) - ($before.probe_miss // 0)),
        probe_body_too_large: (($after.probe_body_too_large // 0) - ($before.probe_body_too_large // 0)),
        probe_l2ready: (($after.probe_l2ready // 0) - ($before.probe_l2ready // 0)),
        probe_timeout_final: (($after.probe_timeout_final // 0) - ($before.probe_timeout_final // 0)),
        probe_latency_avg: ($after.probe_latency_avg // 0),
        probe_latency_max: ($after.probe_latency_max // 0)
      }'
}

run_burst_leg() {
    local leg_name=$1
    local url=$2
    local require_fallback=$3

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
    local fallbacks
    leaders=$(jq -r '.leaders' <<< "$coal_json")
    followers=$(jq -r '.followers' <<< "$coal_json")
    fallbacks=$(jq -r '.fallbacks' <<< "$coal_json")

    if [ "$leaders" -eq 0 ]; then
        echo "ERROR: Expected at least 1 leader request for $leg_name, got 0"
        exit 1
    fi
    if [ "$followers" -eq 0 ]; then
        echo "ERROR: Expected followers for overlapping requests in $leg_name, got 0"
        exit 1
    fi
    if [ "$require_fallback" = "yes" ] && [ "$fallbacks" -eq 0 ]; then
        echo "ERROR: Expected fallback requests for $leg_name, got 0"
        exit 1
    fi
}

LEG_A_URL="${ENVOY_HOST}/products/fast/123?_b=${TIMESTAMP}a"
LEG_B_URL="${ENVOY_HOST}/products/slow/123?_b=${TIMESTAMP}b"

run_burst_leg "Leg A" "$LEG_A_URL" "no"

echo "Cooling down socket pools..."
sleep 3

run_burst_leg "Leg B" "$LEG_B_URL" "yes"

./benchmarks/report/validate.sh "$REPORT_FILE"
./benchmarks/report/generate_json_report.sh "$REPORT_FILE"
./benchmarks/report/generate_markdown_report.sh "${REPORT_FILE%.txt}.json"
./benchmarks/report/check_thresholds.sh "${REPORT_FILE%.txt}.json"

echo "Benchmark complete."
echo "Results saved to: $REPORT_FILE"

#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

# Helper to query prometheus metrics from metrics host
get_metric() {
    local name=$1
    local val
    val=$(curl -s "${METRICS_HOST}/metrics" | grep "^${name} " | awk '{print $2}' || echo "0")
    if [ -z "$val" ]; then val=0; fi
    echo "$val"
}

# Snapshot the current coalescing metrics
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

# Compute deltas of coalescing metrics between two snapshots
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

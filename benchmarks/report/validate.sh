#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

require_json_field() {
    local json_str=$1
    local field=$2
    local context=$3
    local val
    val=$(echo "$json_str" | jq -e "$field" 2>/dev/null || true)
    if [ -z "$val" ] || [ "$val" = "null" ]; then
        echo "ERROR: Missing '$field' in $context" >&2
        return 1
    fi
}

validate_json_lines() {
    local file=$1
    local label=$2
    shift 2
    local found=0

    while IFS= read -r line; do
        if [[ "$line" =~ $label:[[:space:]]*(.*) ]]; then
            found=$((found + 1))
            local json_str="${BASH_REMATCH[1]}"
            echo "Checking $label: $json_str"
            for field in "$@"; do
                require_json_field "$json_str" "$field" "$label block in $file"
            done
        fi
    done < <(grep -E "$label" "$file" || true)

    if [ "$found" -eq 0 ]; then
        echo "ERROR: No $label blocks found in report $file" >&2
        return 1
    fi

    echo "$found"
}

validate_report_file() {
    local file=$1
    if [ ! -f "$file" ]; then
        echo "ERROR: Report file '$file' not found" >&2
        return 1
    fi

    echo "Validating report file: $file"

    validate_json_lines "$file" "Latency JSON" \
        '.latency_ms.p50' '.latency_ms.p95' '.latency_ms.p99' > /dev/null

    validate_json_lines "$file" "Throughput JSON" \
        '.throughput.requests_per_second' '.total_requests' '.successful_requests' '.failed_requests' > /dev/null

    validate_json_lines "$file" "Container Stats JSON" \
        '."envoy".cpu_percent' '."envoy".peak_memory_mb' \
        '."bytetaper-extproc".cpu_percent' '."bytetaper-extproc".peak_memory_mb' \
        '."mock-api".cpu_percent' '."mock-api".peak_memory_mb' > /dev/null

    validate_json_lines "$file" "Payload Savings JSON" \
        '.original_bytes_avg' '.optimized_bytes_avg' '.bytes_saved_avg' '.reduction_ratio' > /dev/null

    local scenario
    scenario=$(grep -E "^Scenario:" "$file" | head -n 1 | awk '{print $2}' || echo "unknown")

    if [[ "$scenario" == *"coalescing_burst"* ]]; then
        local coalescing_found
        coalescing_found=$(validate_json_lines "$file" "Coalescing JSON" \
            '.client_requests_sent' '.upstream_mock_calls' '.leaders' '.followers' \
            '.follower_cache_hits' '.fallbacks' '.bypasses' '.coalescing_ratio' \
            '.upstream_amplification_ratio' '.follower_shared_response' '.follower_l1_hit' \
            '.follower_timeout' '.follower_missing' '.follower_stored_but_no_snapshot' \
            '.follower_not_cacheable' '.follower_failed' '.follower_pool_queue_full' \
            '.follower_unaccounted' '.leader_l1_store_success' '.leader_l1_store_failed' \
            '.follower_l1_ready' '.follower_l1_ready_but_miss' '.follower_expired')

        if [ "$coalescing_found" -lt 2 ]; then
            echo "ERROR: Expected at least 2 coalescing JSON blocks (Leg A and Leg B), found $coalescing_found in report $file" >&2
            return 1
        fi
    fi

    if grep -qE "Mock API Metrics JSON" "$file"; then
        validate_json_lines "$file" "Mock API Metrics JSON" \
            '.service' '.upstream_requests_total' '.responses_total' '.metrics_requests_total' \
            '.bytes_sent_total' '.fast_path_requests_total' '.slow_path_requests_total' \
            '.status_codes' '.methods' '.paths' > /dev/null
    fi

    if [[ "$scenario" == *"coalescing_burst"* ]]; then
        local mock_found
        mock_found=$(grep -cE "Mock API Metrics JSON" "$file" || true)
        if [ "$mock_found" -lt 2 ]; then
            echo "ERROR: Expected mock API metrics for Leg A and Leg B, found $mock_found in report $file" >&2
            return 1
        fi
    fi

    if [[ "$scenario" == *"cache_hit"* ]]; then
        local mock_found
        mock_found=$(grep -cE "Mock API Metrics JSON" "$file" || true)
        if [ "$mock_found" -lt 3 ]; then
            echo "ERROR: Expected mock API metrics for Cache Miss, L1 Cache Hit, and L2 Shared Hit, found $mock_found in report $file" >&2
            return 1
        fi
    fi

    echo "Report $file is fully VALID (all latency, throughput, resource, payload, coalescing, and mock API metrics are present as required)."
    return 0
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    if [ $# -ne 1 ]; then
        echo "Usage: $0 <report_file>" >&2
        exit 1
    fi
    validate_report_file "$1"
fi

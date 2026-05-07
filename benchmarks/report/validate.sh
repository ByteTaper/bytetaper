#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

validate_report_file() {
    local file=$1
    if [ ! -f "$file" ]; then
        echo "ERROR: Report file '$file' not found" >&2
        return 1
    fi

    echo "Validating report file: $file"

    # Find all JSON latency blocks within the report file
    # We look for lines containing "Latency JSON" (e.g., "Latency JSON: { ... }", "Leg A Latency JSON: { ... }")
    local found=0
    while IFS= read -r line; do
        if [[ "$line" =~ Latency\ JSON:[[:space:]]*(.*) ]]; then
            found=1
            local json_str="${BASH_REMATCH[1]}"
            echo "Checking latency JSON: $json_str"

            # Parse using jq and assert fields exist
            local p50
            local p95
            local p99
            p50=$(echo "$json_str" | jq -e '.latency_ms.p50' 2>/dev/null || true)
            p95=$(echo "$json_str" | jq -e '.latency_ms.p95' 2>/dev/null || true)
            p99=$(echo "$json_str" | jq -e '.latency_ms.p99' 2>/dev/null || true)

            if [ -z "$p50" ] || [ "$p50" = "null" ]; then
                echo "ERROR: Missing 'latency_ms.p50' in report $file" >&2
                return 1
            fi
            if [ -z "$p95" ] || [ "$p95" = "null" ]; then
                echo "ERROR: Missing 'latency_ms.p95' in report $file" >&2
                return 1
            fi
            if [ -z "$p99" ] || [ "$p99" = "null" ]; then
                echo "ERROR: Missing 'latency_ms.p99' in report $file" >&2
                return 1
            fi

            echo "  - p50: $p50 ms"
            echo "  - p95: $p95 ms"
            echo "  - p99: $p99 ms"
        fi
    done < <(grep -E "Latency JSON" "$file" || true)

    if [ $found -eq 0 ]; then
        echo "ERROR: No latency JSON blocks found in report $file" >&2
        return 1
    fi

    # Find all JSON throughput blocks within the report file
    # We look for lines containing "Throughput JSON" (e.g., "Throughput JSON: { ... }", "Leg A Throughput JSON: { ... }")
    local tp_found=0
    while IFS= read -r line; do
        if [[ "$line" =~ Throughput\ JSON:[[:space:]]*(.*) ]]; then
            tp_found=1
            local json_str="${BASH_REMATCH[1]}"
            echo "Checking throughput JSON: $json_str"

            # Parse using jq and assert fields exist
            local rps
            local total
            local success
            local failed
            rps=$(echo "$json_str" | jq -e '.throughput.requests_per_second' 2>/dev/null || true)
            total=$(echo "$json_str" | jq -e '.total_requests' 2>/dev/null || true)
            success=$(echo "$json_str" | jq -e '.successful_requests' 2>/dev/null || true)
            failed=$(echo "$json_str" | jq -e '.failed_requests' 2>/dev/null || true)

            if [ -z "$rps" ] || [ "$rps" = "null" ]; then
                echo "ERROR: Missing 'throughput.requests_per_second' in report $file" >&2
                return 1
            fi
            if [ -z "$total" ] || [ "$total" = "null" ]; then
                echo "ERROR: Missing 'total_requests' in report $file" >&2
                return 1
            fi
            if [ -z "$success" ] || [ "$success" = "null" ]; then
                echo "ERROR: Missing 'successful_requests' in report $file" >&2
                return 1
            fi
            if [ -z "$failed" ] || [ "$failed" = "null" ]; then
                echo "ERROR: Missing 'failed_requests' in report $file" >&2
                return 1
            fi

            echo "  - Requests per second: $rps"
            echo "  - Total requests: $total"
            echo "  - Successful requests: $success"
            echo "  - Failed requests: $failed"
        fi
    done < <(grep -E "Throughput JSON" "$file" || true)

    if [ $tp_found -eq 0 ]; then
        echo "ERROR: No throughput JSON blocks found in report $file" >&2
        return 1
    fi

    # Find all JSON container stats blocks within the report file
    # We look for lines containing "Container Stats JSON" (e.g., "Container Stats JSON: { ... }", "Leg A Container Stats JSON: { ... }")
    local stats_found=0
    while IFS= read -r line; do
        if [[ "$line" =~ Container\ Stats\ JSON:[[:space:]]*(.*) ]]; then
            stats_found=1
            local json_str="${BASH_REMATCH[1]}"
            echo "Checking container stats JSON: $json_str"

            # Parse using jq and assert fields exist
            for svc in "envoy" "bytetaper-extproc" "mock-api"; do
                local cpu
                local mem
                cpu=$(echo "$json_str" | jq -r ".\"$svc\".cpu_percent" 2>/dev/null || true)
                mem=$(echo "$json_str" | jq -r ".\"$svc\".peak_memory_mb" 2>/dev/null || true)

                if [ -z "$cpu" ] || [ "$cpu" = "null" ]; then
                    echo "ERROR: Missing '$svc.cpu_percent' in report $file" >&2
                    return 1
                fi
                if [ -z "$mem" ] || [ "$mem" = "null" ]; then
                    echo "ERROR: Missing '$svc.peak_memory_mb' in report $file" >&2
                    return 1
                fi

                echo "  - $svc CPU: $cpu"
                echo "  - $svc Memory: $mem"
            done
        fi
    done < <(grep -E "Container Stats JSON" "$file" || true)

    if [ $stats_found -eq 0 ]; then
        echo "ERROR: No container stats JSON blocks found in report $file" >&2
        return 1
    fi

    # Find all JSON payload savings blocks within the report file
    # We look for lines containing "Payload Savings JSON" (e.g., "Payload Savings JSON: { ... }", "Leg A Payload Savings JSON: { ... }")
    local savings_found=0
    while IFS= read -r line; do
        if [[ "$line" =~ Payload\ Savings\ JSON:[[:space:]]*(.*) ]]; then
            savings_found=1
            local json_str="${BASH_REMATCH[1]}"
            echo "Checking payload savings JSON: $json_str"

            # Parse using jq and assert fields exist
            local orig
            local opt
            local saved
            local ratio
            orig=$(echo "$json_str" | jq -r '.original_bytes_avg' 2>/dev/null || true)
            opt=$(echo "$json_str" | jq -r '.optimized_bytes_avg' 2>/dev/null || true)
            saved=$(echo "$json_str" | jq -r '.bytes_saved_avg' 2>/dev/null || true)
            ratio=$(echo "$json_str" | jq -r '.reduction_ratio' 2>/dev/null || true)

            if [ -z "$orig" ] || [ "$orig" = "null" ]; then
                echo "ERROR: Missing 'original_bytes_avg' in report $file" >&2
                return 1
            fi
            if [ -z "$opt" ] || [ "$opt" = "null" ]; then
                echo "ERROR: Missing 'optimized_bytes_avg' in report $file" >&2
                return 1
            fi
            if [ -z "$saved" ] || [ "$saved" = "null" ]; then
                echo "ERROR: Missing 'bytes_saved_avg' in report $file" >&2
                return 1
            fi
            if [ -z "$ratio" ] || [ "$ratio" = "null" ]; then
                echo "ERROR: Missing 'reduction_ratio' in report $file" >&2
                return 1
            fi

            echo "  - Original bytes: $orig"
            echo "  - Optimized bytes: $opt"
            echo "  - Bytes saved: $saved"
            echo "  - Reduction ratio: $ratio"
        fi
    done < <(grep -E "Payload Savings JSON" "$file" || true)

    if [ $savings_found -eq 0 ]; then
        echo "ERROR: No payload savings JSON blocks found in report $file" >&2
        return 1
    fi
    # 5. Find all JSON coalescing blocks within the report file if the scenario is coalescing_burst
    local scenario
    scenario=$(grep -E "^Scenario:" "$file" | head -n 1 | awk '{print $2}' || echo "unknown")
    if [[ "$scenario" == *"coalescing_burst"* ]]; then
        local coalescing_found=0
        while IFS= read -r line; do
            if [[ "$line" =~ Coalescing\ JSON:[[:space:]]*(.*) ]]; then
                coalescing_found=$((coalescing_found + 1))
                local json_str="${BASH_REMATCH[1]}"
                echo "Checking coalescing JSON: $json_str"

                # Parse using jq and assert fields exist
                for field in "client_requests_sent" "upstream_mock_calls" "leaders" "followers" "follower_cache_hits" "fallbacks" "bypasses" "coalescing_ratio" "upstream_amplification_ratio" \
                             "follower_shared_response" "follower_l1_hit" "follower_timeout" "follower_missing" "follower_stored_but_no_snapshot" \
                             "follower_not_cacheable" "follower_failed" "follower_pool_queue_full" "follower_unaccounted" \
                             "leader_l1_store_success" "leader_l1_store_failed" "follower_l1_ready" "follower_l1_ready_but_miss"; do
                    local val
                    val=$(echo "$json_str" | jq -e ".$field" 2>/dev/null || true)
                    if [ -z "$val" ] || [ "$val" = "null" ]; then
                        echo "ERROR: Missing '$field' in Coalescing JSON of report $file" >&2
                        return 1
                    fi
                done
            fi
        done < <(grep -E "Coalescing JSON" "$file" || true)

        if [ $coalescing_found -lt 2 ]; then
            echo "ERROR: Expected at least 2 coalescing JSON blocks (Leg A and Leg B), found $coalescing_found in report $file" >&2
            return 1
        fi
    fi

    echo "Report $file is fully VALID (all percentiles, throughput, container stats, payload savings, and coalescing metrics present)."
    return 0
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    if [ $# -ne 1 ]; then
        echo "Usage: $0 <report_file>" >&2
        exit 1
    fi
    validate_report_file "$1"
fi

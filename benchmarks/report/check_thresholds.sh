#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Accept JSON report file as argument
JSON_FILE="${1:-}"
THRESHOLDS_FILE="benchmarks/performance-thresholds.yaml"

if [ -z "$JSON_FILE" ]; then
    echo "Usage: $0 <report_file.json>" >&2
    exit 1
fi

if [ ! -f "$JSON_FILE" ]; then
    echo "ERROR: Report file '$JSON_FILE' not found" >&2
    exit 1
fi

if [ ! -f "$THRESHOLDS_FILE" ]; then
    echo "ERROR: Thresholds configuration '$THRESHOLDS_FILE' not found" >&2
    exit 1
fi

# Extract scenario name
scenario=$(jq -r '.scenario' "$JSON_FILE" || echo "unknown")

if [ "$scenario" = "unknown" ] || [ -z "$scenario" ]; then
    echo "ERROR: Failed to extract scenario name from JSON report." >&2
    exit 1
fi

echo "=== Running Performance Threshold Validation ==="
echo "Scenario: $scenario"
echo "Report: $JSON_FILE"
echo ""

# Helper to retrieve threshold using AWK
get_threshold() {
    local scen=$1
    local key=$2
    awk -v s="$scen" -v k="$key" '
        $0 ~ "^" s ":" { in_scen=1; next }
        in_scen && /^[^ ]/ { in_scen=0 }
        in_scen && $1 ~ k {
            sub(/:/, "", $1)
            print $2
            exit
        }
    ' "$THRESHOLDS_FILE"
}

# Helper to compare floats using AWK
is_greater() {
    awk -v a="$1" -v b="$2" 'BEGIN { exit (a > b ? 0 : 1) }'
}

is_less() {
    awk -v a="$1" -v b="$2" 'BEGIN { exit (a < b ? 0 : 1) }'
}

# Fetch configured thresholds
max_p95=$(get_threshold "$scenario" "max_p95_overhead_ms")
max_error=$(get_threshold "$scenario" "max_error_rate")
min_ratio=$(get_threshold "$scenario" "min_payload_reduction_ratio")



# If no thresholds configured, exit success
if [ -z "$max_p95" ]; then
    echo "WARNING: No performance thresholds configured for scenario '$scenario'."
    exit 0
fi

echo "Configured Thresholds for '$scenario':"
echo "  - Max P95 Latency: ${max_p95} ms"
echo "  - Max Error Rate: ${max_error}"
echo "  - Min Payload Reduction Ratio: ${min_ratio}%"
echo ""

failed_checks=0

# Iterate through legs
legs=$(jq -r '.latency_ms | keys[]' "$JSON_FILE" || echo "main")

while IFS= read -r leg; do
    if [ -z "$leg" ]; then continue; fi
    echo "Checking metrics for Leg: '$leg'..."

    # 1. P95 Latency Check
    p95=$(jq -r ".latency_ms.\"$leg\".latency_ms.p95" "$JSON_FILE" || echo "unavailable")
    if [ "$p95" != "unavailable" ] && [ -n "$p95" ] && [ "$p95" != "null" ]; then
        if is_greater "$p95" "$max_p95"; then
            echo "  [FAIL] P95 Latency: ${p95} ms (Threshold exceeded: max ${max_p95} ms)" >&2
            failed_checks=$((failed_checks + 1))
        else
            echo "  [PASS] P95 Latency: ${p95} ms (Threshold: max ${max_p95} ms)"
        fi
    else
        echo "  [SKIP] P95 Latency is unavailable."
    fi

    # 2. Error Rate Check
    total_reqs=$(jq -r ".throughput.\"$leg\".total_requests" "$JSON_FILE" || echo "0")
    failed_reqs=$(jq -r ".throughput.\"$leg\".failed_requests" "$JSON_FILE" || echo "0")

    if [ "$total_reqs" != "unavailable" ] && [ "$total_reqs" != "null" ] && [ -n "$total_reqs" ] && [[ "$total_reqs" =~ ^[0-9]+$ ]] && [ "$total_reqs" -gt 0 ]; then
        error_rate=$(awk -v f="$failed_reqs" -v t="$total_reqs" 'BEGIN { print (f / t) }')
        if is_greater "$error_rate" "$max_error"; then
            echo "  [FAIL] Error Rate: ${error_rate} (Threshold exceeded: max ${max_error})" >&2
            failed_checks=$((failed_checks + 1))
        else
            echo "  [PASS] Error Rate: ${error_rate} (Threshold: max ${max_error})"
        fi
    else
        echo "  [SKIP] Error Rate Check (Total requests unavailable or 0)."
    fi

    # 3. Payload Reduction Ratio Check
    ratio_str=$(jq -r ".payload.\"$leg\".reduction_ratio" "$JSON_FILE" || echo "0.00%")
    if [ "$ratio_str" != "unavailable" ] && [ -n "$ratio_str" ] && [ "$ratio_str" != "null" ]; then
        # Strip trailing % sign
        ratio_val=$(echo "$ratio_str" | sed 's/%//')
        if is_less "$ratio_val" "$min_ratio"; then
            echo "  [FAIL] Payload Reduction: ${ratio_str} (Threshold not met: min ${min_ratio}%)" >&2
            failed_checks=$((failed_checks + 1))
        else
            echo "  [PASS] Payload Reduction: ${ratio_str} (Threshold: min ${min_ratio}%)"
        fi
    else
        echo "  [SKIP] Payload Reduction is unavailable."
    fi
    echo ""

done < <(jq -r '.latency_ms | keys[]' "$JSON_FILE" || echo "main")

# Coalescing correctness checks (coalescing_burst only)
if [ "$scenario" = "coalescing_burst" ]; then
    min_coal_ratio=$(get_threshold "$scenario" "min_coalescing_ratio_leg_a")
    max_unaccounted=$(get_threshold "$scenario" "max_follower_unaccounted_leg_a")
    max_missing=$(get_threshold "$scenario" "max_follower_missing_leg_a")
    min_fb_b=$(get_threshold "$scenario" "min_fallbacks_leg_b")

    for leg in "Leg A" "Leg B"; do
        coal_ratio=$(jq -r --arg l "$leg" '.coalescing[$l].coalescing_ratio // "n/a"' "$JSON_FILE")
        unaccounted=$(jq -r --arg l "$leg" '.coalescing[$l].follower_unaccounted // "n/a"' "$JSON_FILE")
        missing=$(jq -r --arg l "$leg" '.coalescing[$l].follower_missing // "n/a"' "$JSON_FILE")
        fallbacks=$(jq -r --arg l "$leg" '.coalescing[$l].fallbacks // "n/a"' "$JSON_FILE")

        if [ "$leg" = "Leg A" ]; then
            # Min coalescing ratio
            if [ "$coal_ratio" != "n/a" ] && [ -n "$min_coal_ratio" ] && [ "$coal_ratio" != "null" ]; then
                if is_less "$coal_ratio" "$min_coal_ratio"; then
                    echo "  [FAIL] $leg coalescing_ratio: $coal_ratio (min $min_coal_ratio)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg coalescing_ratio: $coal_ratio (Threshold: min $min_coal_ratio)"
                fi
            fi
            # Max unaccounted
            if [ "$unaccounted" != "n/a" ] && [ -n "$max_unaccounted" ] && [ "$unaccounted" != "null" ]; then
                if [ "$unaccounted" -gt "$max_unaccounted" ]; then
                    echo "  [FAIL] $leg follower_unaccounted: $unaccounted (max $max_unaccounted)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg follower_unaccounted: $unaccounted (Threshold: max $max_unaccounted)"
                fi
            fi
            # Max missing
            if [ "$missing" != "n/a" ] && [ -n "$max_missing" ] && [ "$missing" != "null" ]; then
                if [ "$missing" -gt "$max_missing" ]; then
                    echo "  [FAIL] $leg follower_missing: $missing (max $max_missing)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg follower_missing: $missing (Threshold: max $max_missing)"
                fi
            fi
        fi

        if [ "$leg" = "Leg B" ] && [ -n "$min_fb_b" ] && [ "$min_fb_b" != "null" ]; then
            if [ "$fallbacks" != "n/a" ] && [ "$fallbacks" != "null" ] && [ "$fallbacks" -lt "$min_fb_b" ]; then
                echo "  [FAIL] $leg fallbacks: $fallbacks (expected >= $min_fb_b on slow path)" >&2
                failed_checks=$((failed_checks + 1))
            else
                echo "  [PASS] $leg fallbacks: $fallbacks (Threshold: >= $min_fb_b)"
            fi
        fi
    done
fi

if [ "$failed_checks" -gt 0 ]; then
    echo "=== Threshold Validation FAILED ($failed_checks breaches detected) ===" >&2
    exit 1
else
    echo "=== Threshold Validation PASSED (all checks succeeded) ==="
    exit 0
fi

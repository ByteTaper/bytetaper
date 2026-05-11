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
            sub(/^[^:]*:[[:space:]]*/, "", $0)
            sub(/[[:space:]]*#.*/, "", $0)
            gsub(/^"|"$|^'\''|'\''$/, "", $0)
            gsub(/[[:space:]]+$/, "", $0)
            print $0
            exit
        }
    ' "$THRESHOLDS_FILE"
}

# Helper to compare floats using AWK
is_greater() {
    awk -v a="$1" -v b="$2" 'BEGIN { exit (a > b ? 0 : 1) }'
}

# Helper to compare floats using AWK
is_less() {
    awk -v a="$1" -v b="$2" 'BEGIN { exit (a < b ? 0 : 1) }'
}

# Fetch configured thresholds
max_p95_ms=$(get_threshold "$scenario" "max_p95_ms")
max_p95_overhead_ms=$(get_threshold "$scenario" "max_p95_overhead_ms")
max_p95_overhead_ratio=$(get_threshold "$scenario" "max_p95_overhead_ratio")
max_error=$(get_threshold "$scenario" "max_error_rate")
min_ratio=$(get_threshold "$scenario" "min_payload_reduction_ratio")

# If no thresholds configured, exit success
if [ -z "$max_p95_ms" ] && [ -z "$max_p95_overhead_ms" ] && [ -z "$max_p95_overhead_ratio" ] && [ -z "$max_error" ] && [ -z "$min_ratio" ]; then
    echo "WARNING: No performance thresholds configured for scenario '$scenario'."
    exit 0
fi

echo "Configured Thresholds for '$scenario':"
if [ -n "$max_p95_ms" ]; then
    echo "  - Max P95 Latency (Absolute): ${max_p95_ms} ms"
fi
if [ -n "$max_p95_overhead_ms" ] || [ -n "$max_p95_overhead_ratio" ]; then
    if [ -n "$max_p95_overhead_ms" ]; then
        echo "  - Max P95 Overhead: ${max_p95_overhead_ms} ms"
    fi
    if [ -n "$max_p95_overhead_ratio" ]; then
        echo "  - Max P95 Overhead Ratio: ${max_p95_overhead_ratio}x"
    fi
    baseline_leg_cfg=$(get_threshold "$scenario" "baseline_leg")
    target_leg_cfg=$(get_threshold "$scenario" "target_leg")
    baseline_leg_cfg="${baseline_leg_cfg:-Baseline}"
    target_leg_cfg="${target_leg_cfg:-Observe}"
    echo "    - Baseline Leg: '${baseline_leg_cfg}'"
    echo "    - Target Leg: '${target_leg_cfg}'"
fi
echo "  - Max Error Rate: ${max_error}"
echo "  - Min Payload Reduction Ratio: ${min_ratio}%"
echo ""

failed_checks=0

# 1. P95 Overhead Latency Check (done once outside the per-leg loop)
if [ -n "$max_p95_overhead_ms" ] || [ -n "$max_p95_overhead_ratio" ]; then
    baseline_leg_cfg=$(get_threshold "$scenario" "baseline_leg")
    target_leg_cfg=$(get_threshold "$scenario" "target_leg")
    baseline_leg_cfg="${baseline_leg_cfg:-Baseline}"
    target_leg_cfg="${target_leg_cfg:-Observe}"

    baseline_p95=$(jq -r ".latency_ms.\"$baseline_leg_cfg\".latency_ms.p95" "$JSON_FILE" || echo "null")
    target_p95=$(jq -r ".latency_ms.\"$target_leg_cfg\".latency_ms.p95" "$JSON_FILE" || echo "null")

    if [ "$baseline_p95" = "null" ] || [ -z "$baseline_p95" ] || [ "$baseline_p95" = "unavailable" ] || [ "$baseline_p95" = "0" ] || [ "$baseline_p95" = "0.0" ]; then
        echo "ERROR: Overhead threshold configured but baseline leg '$baseline_leg_cfg' data is missing or invalid ($baseline_p95)." >&2
        failed_checks=$((failed_checks + 1))
    elif [ "$target_p95" = "null" ] || [ -z "$target_p95" ] || [ "$target_p95" = "unavailable" ]; then
        echo "ERROR: Overhead threshold configured but target leg '$target_leg_cfg' data is missing or unavailable." >&2
        failed_checks=$((failed_checks + 1))
    else
        echo "Checking Overhead for target leg '$target_leg_cfg' against baseline leg '$baseline_leg_cfg':"
        
        # Absolute overhead ms check
        if [ -n "$max_p95_overhead_ms" ]; then
            overhead_ms=$(awk -v t="$target_p95" -v b="$baseline_p95" 'BEGIN {
                diff = t - b
                if (diff < 0) diff = 0.0
                print diff
            }')

            if is_greater "$overhead_ms" "$max_p95_overhead_ms"; then
                echo "  [FAIL] P95 Overhead: ${overhead_ms} ms (Baseline: ${baseline_p95} ms, Target: ${target_p95} ms) (Threshold exceeded: max ${max_p95_overhead_ms} ms)" >&2
                failed_checks=$((failed_checks + 1))
            else
                echo "  [PASS] P95 Overhead: ${overhead_ms} ms (Baseline: ${baseline_p95} ms, Target: ${target_p95} ms) (Threshold: max ${max_p95_overhead_ms} ms)"
            fi
        fi

        # Ratio-based overhead check
        if [ -n "$max_p95_overhead_ratio" ]; then
            overhead_ratio=$(awk -v t="$target_p95" -v b="$baseline_p95" 'BEGIN {
                ratio = t / b
                print ratio
            }')

            if is_greater "$overhead_ratio" "$max_p95_overhead_ratio"; then
                echo "  [FAIL] P95 Overhead Ratio: ${overhead_ratio}x (Baseline: ${baseline_p95} ms, Target: ${target_p95} ms) (Threshold exceeded: max ${max_p95_overhead_ratio}x)" >&2
                failed_checks=$((failed_checks + 1))
            else
                echo "  [PASS] P95 Overhead Ratio: ${overhead_ratio}x (Baseline: ${baseline_p95} ms, Target: ${target_p95} ms) (Threshold: max ${max_p95_overhead_ratio}x)"
            fi
        fi
    fi
    echo ""
fi

# Iterate through legs
legs=$(jq -r '.latency_ms | keys[]' "$JSON_FILE" || echo "main")

while IFS= read -r leg; do
    if [ -z "$leg" ]; then continue; fi
    echo "Checking metrics for Leg: '$leg'..."

    # 1. Absolute P95 Latency Check (if max_p95_ms is configured)
    if [ -n "$max_p95_ms" ]; then
        skip_absolute=0
        if [ -n "$max_p95_overhead_ms" ] || [ -n "$max_p95_overhead_ratio" ]; then
            baseline_leg_cfg=$(get_threshold "$scenario" "baseline_leg")
            target_leg_cfg=$(get_threshold "$scenario" "target_leg")
            baseline_leg_cfg="${baseline_leg_cfg:-Baseline}"
            target_leg_cfg="${target_leg_cfg:-Observe}"
            if [ "$leg" = "$baseline_leg_cfg" ] || [ "$leg" = "$target_leg_cfg" ]; then
                skip_absolute=1
            fi
        fi

        if [ "$skip_absolute" -eq 1 ]; then
            echo "  [SKIP] P95 Latency (relative overhead applied instead)."
        else
            p95=$(jq -r ".latency_ms.\"$leg\".latency_ms.p95" "$JSON_FILE" || echo "unavailable")
            if [ "$p95" != "unavailable" ] && [ -n "$p95" ] && [ "$p95" != "null" ]; then
                if is_greater "$p95" "$max_p95_ms"; then
                    echo "  [FAIL] P95 Latency: ${p95} ms (Threshold exceeded: max ${max_p95_ms} ms)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] P95 Latency: ${p95} ms (Threshold: max ${max_p95_ms} ms)"
                fi
            else
                echo "  [SKIP] P95 Latency is unavailable."
            fi
        fi
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
    max_amp_a=$(get_threshold "$scenario" "max_upstream_amplification_ratio_leg_a")
    warn_p99_ms=$(get_threshold "$scenario" "warn_p99_ms")

    for leg in "Leg A" "Leg B"; do
        coal_ratio=$(jq -r --arg l "$leg" '.coalescing[$l].coalescing_ratio // "n/a"' "$JSON_FILE")
        unaccounted=$(jq -r --arg l "$leg" '.coalescing[$l].follower_unaccounted // "n/a"' "$JSON_FILE")
        missing=$(jq -r --arg l "$leg" '.coalescing[$l].follower_missing // "n/a"' "$JSON_FILE")
        fallbacks=$(jq -r --arg l "$leg" '.coalescing[$l].fallbacks // "n/a"' "$JSON_FILE")

        # Warning P99 Latency check
        if [ -n "$warn_p99_ms" ] && [ "$warn_p99_ms" != "null" ]; then
            p99=$(jq -r ".latency_ms.\"$leg\".latency_ms.p99" "$JSON_FILE" || echo "unavailable")
            if [ "$p99" != "unavailable" ] && [ -n "$p99" ] && [ "$p99" != "null" ]; then
                if is_greater "$p99" "$warn_p99_ms"; then
                    echo "  [WARN] $leg P99 Latency: ${p99} ms (Warning threshold exceeded: max ${warn_p99_ms} ms)"
                fi
            fi
        fi

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
            # Upstream amplification check
            amp_ratio=$(jq -r --arg l "$leg" '.coalescing[$l].upstream_amplification_ratio // "n/a"' "$JSON_FILE")
            if [ "$amp_ratio" != "n/a" ] && [ -n "$max_amp_a" ] && [ "$amp_ratio" != "null" ]; then
                if is_greater "$amp_ratio" "$max_amp_a"; then
                    echo "  [FAIL] $leg upstream_amplification_ratio: $amp_ratio (max $max_amp_a)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg upstream_amplification_ratio: $amp_ratio (Threshold: max $max_amp_a)"
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

if [ "$scenario" = "coalescing_body_size_tiers" ]; then
    max_calls=$(get_threshold "$scenario" "max_upstream_calls_per_leg")
    max_unaccounted=$(get_threshold "$scenario" "max_follower_unaccounted")
    max_missing=$(get_threshold "$scenario" "max_follower_missing")

    # L1-Inline thresholds
    l1_min_hit_ratio=$(get_threshold "$scenario" "l1_inline_min_follower_l1_hit_ratio")
    l1_max_p95=$(get_threshold "$scenario" "l1_inline_max_p95_ms")

    # L2-Completion thresholds
    l2c_min_probe_hits=$(get_threshold "$scenario" "l2_completion_min_probe_hit_total")
    l2c_max_p95=$(get_threshold "$scenario" "l2_completion_max_p95_ms")

    # L2-Warm-Only thresholds
    l2w_min_fallbacks=$(get_threshold "$scenario" "l2_warmonly_min_fallbacks")
    l2w_max_p95=$(get_threshold "$scenario" "l2_warmonly_max_p95_ms")
    l2w_warn_calls=$(get_threshold "$scenario" "l2_warmonly_warn_upstream_calls")

    for leg in "Leg L1-Inline" "Leg L2-Completion" "Leg L2-Warm-Only"; do
        echo "Validating specialized metrics for Leg: '$leg'..."

        # 1. Base checks (upstream calls, unaccounted, missing)
        upstream_calls=$(jq -r --arg l "$leg" '.coalescing[$l].upstream_mock_calls // "n/a"' "$JSON_FILE")
        unaccounted=$(jq -r --arg l "$leg" '.coalescing[$l].follower_unaccounted // "n/a"' "$JSON_FILE")
        missing=$(jq -r --arg l "$leg" '.coalescing[$l].follower_missing // "n/a"' "$JSON_FILE")
        p95=$(jq -r ".latency_ms.\"$leg\".latency_ms.p95" "$JSON_FILE" || echo "n/a")

        if [ "$leg" != "Leg L2-Warm-Only" ] && [ "$upstream_calls" != "n/a" ] && [ -n "$max_calls" ] && [ "$upstream_calls" != "null" ]; then
            if [ "$upstream_calls" -gt "$max_calls" ]; then
                echo "  [FAIL] $leg upstream calls: $upstream_calls (max $max_calls)" >&2
                failed_checks=$((failed_checks + 1))
            else
                echo "  [PASS] $leg upstream calls: $upstream_calls (Threshold: max $max_calls)"
            fi
        fi

        if [ "$unaccounted" != "n/a" ] && [ -n "$max_unaccounted" ] && [ "$unaccounted" != "null" ]; then
            if [ "$unaccounted" -gt "$max_unaccounted" ]; then
                echo "  [FAIL] $leg follower_unaccounted: $unaccounted (max $max_unaccounted)" >&2
                failed_checks=$((failed_checks + 1))
            else
                echo "  [PASS] $leg follower_unaccounted: $unaccounted (Threshold: max $max_unaccounted)"
            fi
        fi

        if [ "$missing" != "n/a" ] && [ -n "$max_missing" ] && [ "$missing" != "null" ]; then
            if [ "$missing" -gt "$max_missing" ]; then
                echo "  [FAIL] $leg follower_missing: $missing (max $max_missing)" >&2
                failed_checks=$((failed_checks + 1))
            else
                echo "  [PASS] $leg follower_missing: $missing (Threshold: max $max_missing)"
            fi
        fi

        # 2. Leg-specific checks
        if [ "$leg" = "Leg L1-Inline" ]; then
            # P95 latency check for inline
            if [ "$p95" != "n/a" ] && [ -n "$l1_max_p95" ] && [ "$p95" != "null" ]; then
                if is_greater "$p95" "$l1_max_p95"; then
                    echo "  [FAIL] $leg p95 latency: $p95 ms (max $l1_max_p95 ms)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg p95 latency: $p95 ms (Threshold: max $l1_max_p95 ms)"
                fi
            fi

            # Follower L1 hit ratio check
            followers=$(jq -r --arg l "$leg" '.coalescing[$l].followers // "0"' "$JSON_FILE")
            l1_hit=$(jq -r --arg l "$leg" '.coalescing[$l].follower_l1_hit // "0"' "$JSON_FILE")
            if [ "$followers" -gt 0 ] && [ -n "$l1_min_hit_ratio" ] && [ "$followers" != "null" ]; then
                hit_ratio=$(awk -v h="$l1_hit" -v f="$followers" 'BEGIN { print h / f }')
                if is_less "$hit_ratio" "$l1_min_hit_ratio"; then
                    echo "  [FAIL] $leg follower_l1_hit_ratio: $hit_ratio (min $l1_min_hit_ratio)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg follower_l1_hit_ratio: $hit_ratio (Threshold: min $l1_min_hit_ratio)"
                fi
            fi
        fi

        if [ "$leg" = "Leg L2-Completion" ]; then
            # P95 latency check
            if [ "$p95" != "n/a" ] && [ -n "$l2c_max_p95" ] && [ "$p95" != "null" ]; then
                if is_greater "$p95" "$l2c_max_p95"; then
                    echo "  [FAIL] $leg p95 latency: $p95 ms (max $l2c_max_p95 ms)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg p95 latency: $p95 ms (Threshold: max $l2c_max_p95 ms)"
                fi
            fi

            # Sync L2 probe hit check
            probe_hit=$(jq -r --arg l "$leg" '.coalescing[$l].probe_hit // "n/a"' "$JSON_FILE")
            if [ "$probe_hit" != "n/a" ] && [ -n "$l2c_min_probe_hits" ] && [ "$probe_hit" != "null" ]; then
                if [ "$probe_hit" -lt "$l2c_min_probe_hits" ]; then
                    echo "  [FAIL] $leg sync L2 probe hit: $probe_hit (min $l2c_min_probe_hits)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg sync L2 probe hit: $probe_hit (Threshold: min $l2c_min_probe_hits)"
                fi
            fi
        fi

        if [ "$leg" = "Leg L2-Warm-Only" ]; then
            # P95 latency check
            if [ "$p95" != "n/a" ] && [ -n "$l2w_max_p95" ] && [ "$p95" != "null" ]; then
                if is_greater "$p95" "$l2w_max_p95"; then
                    echo "  [FAIL] $leg p95 latency: $p95 ms (max $l2w_max_p95 ms)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg p95 latency: $p95 ms (Threshold: max $l2w_max_p95 ms)"
                fi
            fi

            # Fallback check
            fallbacks=$(jq -r --arg l "$leg" '.coalescing[$l].fallbacks // "n/a"' "$JSON_FILE")
            if [ "$fallbacks" != "n/a" ] && [ -n "$l2w_min_fallbacks" ] && [ "$fallbacks" != "null" ]; then
                if [ "$fallbacks" -lt "$l2w_min_fallbacks" ]; then
                    echo "  [FAIL] $leg fallbacks: $fallbacks (min $l2w_min_fallbacks)" >&2
                    failed_checks=$((failed_checks + 1))
                else
                    echo "  [PASS] $leg fallbacks: $fallbacks (Threshold: min $l2w_min_fallbacks)"
                fi
            fi

            # Upstream calls warning
            if [ "$upstream_calls" != "n/a" ] && [ -n "$l2w_warn_calls" ] && [ "$upstream_calls" != "null" ]; then
                if [ "$upstream_calls" -gt "$l2w_warn_calls" ]; then
                    echo "  [WARN] $leg upstream calls: $upstream_calls (exceeds warning threshold $l2w_warn_calls)"
                fi
            fi
        fi
    done
fi

# Add structured overhead and threshold results to the JSON report
baseline_leg_cfg=$(get_threshold "$scenario" "baseline_leg")
target_leg_cfg=$(get_threshold "$scenario" "target_leg")
if [ -n "$baseline_leg_cfg" ] && [ -n "$target_leg_cfg" ]; then
    baseline_p95=$(jq -r ".latency_ms.\"$baseline_leg_cfg\".latency_ms.p95" "$JSON_FILE" || echo "null")
    target_p95=$(jq -r ".latency_ms.\"$target_leg_cfg\".latency_ms.p95" "$JSON_FILE" || echo "null")
    if [ "$baseline_p95" != "null" ] && [ "$baseline_p95" != "unavailable" ] && [ -n "$baseline_p95" ] && \
       [ "$target_p95" != "null" ] && [ "$target_p95" != "unavailable" ] && [ -n "$target_p95" ]; then
        overhead_ms=$(awk -v t="$target_p95" -v b="$baseline_p95" 'BEGIN {
            diff = t - b
            if (diff < 0) diff = 0.0
            print diff
        }')
        overhead_ratio=$(awk -v t="$target_p95" -v b="$baseline_p95" 'BEGIN {
            ratio = t / b
            printf "%.6f", ratio
        }')

        tmp_json=$(mktemp)
        jq --arg b "$baseline_leg_cfg" \
           --arg t "$target_leg_cfg" \
           --argjson bp "$baseline_p95" \
           --argjson tp "$target_p95" \
           --argjson oh_ms "$overhead_ms" \
           --argjson oh_ratio "$overhead_ratio" \
           --arg fc "$failed_checks" \
           '. + {
               overhead_analysis: {
                   baseline_leg: $b,
                   target_leg: $t,
                   baseline_p95_ms: $bp,
                   target_p95_ms: $tp,
                   overhead_ms: $oh_ms,
                   overhead_ratio: $oh_ratio
               },
               threshold_validation: {
                   passed: ($fc == "0"),
                   failed_checks_count: ($fc | tonumber)
               }
           }' "$JSON_FILE" > "$tmp_json"
        mv "$tmp_json" "$JSON_FILE"
    fi
fi

if [ "$failed_checks" -gt 0 ]; then
    echo "=== Threshold Validation FAILED ($failed_checks breaches detected) ===" >&2
    exit 1
else
    echo "=== Threshold Validation PASSED (all checks succeeded) ==="
    exit 0
fi

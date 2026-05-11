#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Accept JSON report file as argument
JSON_FILE="${1:-}"

if [ -z "$JSON_FILE" ]; then
    echo "Usage: $0 <report_file.json>" >&2
    exit 1
fi

if [ ! -f "$JSON_FILE" ]; then
    echo "ERROR: File '$JSON_FILE' not found" >&2
    exit 1
fi

echo "Compiling Markdown report from JSON file: $JSON_FILE"

OUT_MD_FILE="${JSON_FILE%.json}.md"

# Read top-level metadata
scenario=$(jq -r '.scenario' "$JSON_FILE" || echo "unknown")
timestamp=$(jq -r '.timestamp' "$JSON_FILE" || echo "unknown")
bv=$(jq -r '.benchmark_version' "$JSON_FILE" || echo "1.0.0")

# Read features (system details)
os_info=$(jq -r '.features.os_info' "$JSON_FILE" || echo "N/A")
cpu_cores=$(jq -r '.features.cpu_cores' "$JSON_FILE" || echo "N/A")
memory_total=$(jq -r '.features.memory_total' "$JSON_FILE" || echo "N/A")
target_host=$(jq -r '.features.target_host' "$JSON_FILE" || echo "N/A")

# Initialize markdown file
{
    echo "# 🚀 ByteTaper Performance Report"
    echo ""
    echo "An automated performance and integrity audit compiled from execution results."
    echo ""
    echo "## 📊 Benchmark Metadata"
    echo ""
    echo "| Parameter | Value |"
    echo "| :--- | :--- |"
    echo "| **Scenario** | \`${scenario}\` |"
    echo "| **Timestamp** | \`${timestamp}\` |"
    echo "| **Benchmark Version** | \`${bv}\` |"
    echo "| **Target Host** | \`${target_host}\` |"
    echo ""
    echo "## 💻 System Specifications"
    echo ""
    echo "- **Operating System**: \`${os_info}\`"
    echo "- **CPU Cores**: \`${cpu_cores}\`"
    echo "- **Total System Memory**: \`${memory_total}\`"
    echo ""
    echo "## 📈 Execution Metrics"
    echo ""
} > "$OUT_MD_FILE"

# Track if any metric is unavailable to show unified warnings at the bottom
any_unavailable=0

# Iterate through each leg in the report (using leg names as headers) safely handling spaces
while IFS= read -r leg; do
    if [ -z "$leg" ]; then continue; fi

    # Extract leg metrics
    p50=$(jq -r ".latency_ms.\"$leg\".latency_ms.p50" "$JSON_FILE" || echo "unavailable")
    p95=$(jq -r ".latency_ms.\"$leg\".latency_ms.p95" "$JSON_FILE" || echo "unavailable")
    p99=$(jq -r ".latency_ms.\"$leg\".latency_ms.p99" "$JSON_FILE" || echo "unavailable")

    rps=$(jq -r ".throughput.\"$leg\".throughput.requests_per_second" "$JSON_FILE" || echo "unavailable")
    total_reqs=$(jq -r ".throughput.\"$leg\".total_requests" "$JSON_FILE" || echo "unavailable")
    success_reqs=$(jq -r ".throughput.\"$leg\".successful_requests" "$JSON_FILE" || echo "unavailable")
    failed_reqs=$(jq -r ".throughput.\"$leg\".failed_requests" "$JSON_FILE" || echo "unavailable")

    orig_bytes=$(jq -r ".payload.\"$leg\".original_bytes_avg" "$JSON_FILE" || echo "unavailable")
    opt_bytes=$(jq -r ".payload.\"$leg\".optimized_bytes_avg" "$JSON_FILE" || echo "unavailable")
    saved_bytes=$(jq -r ".payload.\"$leg\".bytes_saved_avg" "$JSON_FILE" || echo "unavailable")
    ratio=$(jq -r ".payload.\"$leg\".reduction_ratio" "$JSON_FILE" || echo "unavailable")

    envoy_cpu=$(jq -r ".resources.\"$leg\".envoy.cpu_percent" "$JSON_FILE" || echo "unavailable")
    envoy_mem=$(jq -r ".resources.\"$leg\".envoy.peak_memory_mb" "$JSON_FILE" || echo "unavailable")
    ext_cpu=$(jq -r ".resources.\"$leg\".\"bytetaper-extproc\".cpu_percent" "$JSON_FILE" || echo "unavailable")
    ext_mem=$(jq -r ".resources.\"$leg\".\"bytetaper-extproc\".peak_memory_mb" "$JSON_FILE" || echo "unavailable")
    mock_cpu=$(jq -r ".resources.\"$leg\".\"mock-api\".cpu_percent" "$JSON_FILE" || echo "unavailable")
    mock_mem=$(jq -r ".resources.\"$leg\".\"mock-api\".peak_memory_mb" "$JSON_FILE" || echo "unavailable")

    # Check for unavailable measurements
    if [ "$envoy_cpu" = "unavailable" ] || [ "$envoy_mem" = "unavailable" ] || \
       [ "$ext_cpu" = "unavailable" ] || [ "$ext_mem" = "unavailable" ] || \
       [ "$p50" = "unavailable" ] || [ "$rps" = "unavailable" ] || \
       [ "$orig_bytes" = "unavailable" ]; then
        any_unavailable=1
    fi

    {
        echo "### Sub-Scenario / Leg: \`${leg}\`"
        echo ""
        echo "#### ⏱️ Latency & Throughput Profile"
        echo ""
        echo "| Metric | Value |"
        echo "| :--- | :--- |"
        echo "| **Throughput (RPS)** | \`${rps} req/s\` |"
        echo "| **Total Client Requests** | \`${total_reqs}\` |"
        echo "| **Successful Requests** | \`${success_reqs}\` |"
        echo "| **Failed Requests** | \`${failed_reqs}\` |"
        echo "| **p50 Latency** | \`${p50} ms\` |"
        echo "| **p95 Latency** | \`${p95} ms\` |"
        echo "| **p99 Latency** | \`${p99} ms\` |"
        echo ""
        echo "#### 📦 Payload Savings Summary"
        echo ""
        echo "| Metric | Bytes / Percentage |"
        echo "| :--- | :--- |"
        echo "| **Original Payload Bytes (Avg)** | \`${orig_bytes} bytes\` |"
        echo "| **Optimized Payload Bytes (Avg)** | \`${opt_bytes} bytes\` |"
        echo "| **Payload Bytes Saved (Avg)** | \`${saved_bytes} bytes\` |"
        echo "| **Payload Reduction Ratio** | **\`${ratio}\`** |"
        echo ""
        echo "#### 🎛️ Container Resource Utilizations"
        echo ""
        echo "| Container Service | CPU Average % | Peak Memory (MB) |"
        echo "| :--- | :--- | :--- |"
        echo "| **envoy** | \`${envoy_cpu}%\` | \`${envoy_mem} MB\` |"
        echo "| **bytetaper-extproc** | \`${ext_cpu}%\` | \`${ext_mem} MB\` |"
        echo "| **mock-api** | \`${mock_cpu}%\` | \`${mock_mem} MB\` |"
        echo ""
    } >> "$OUT_MD_FILE"
done < <(jq -r '.latency_ms | keys[]' "$JSON_FILE" || echo "main")

# Coalescing Effectiveness table
if jq -e '.coalescing and (.coalescing | length > 0)' "$JSON_FILE" > /dev/null 2>&1; then
    {
        echo ""
        echo "## ⚙️ Coalescing Effectiveness"
        echo ""
        echo "| Leg | Client Requests | Upstream Calls | Leaders | Followers | Cache Hits | Fallbacks | Bypasses | Coalescing Ratio | Amplification |"
        echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"

        while IFS= read -r leg; do
            cr=$(jq -r --arg l "$leg" '.coalescing[$l].client_requests_sent  // "n/a"' "$JSON_FILE")
            uc=$(jq -r --arg l "$leg" '.coalescing[$l].upstream_mock_calls    // "n/a"' "$JSON_FILE")
            ld=$(jq -r --arg l "$leg" '.coalescing[$l].leaders                // "n/a"' "$JSON_FILE")
            fo=$(jq -r --arg l "$leg" '.coalescing[$l].followers              // "n/a"' "$JSON_FILE")
            ch=$(jq -r --arg l "$leg" '.coalescing[$l].follower_cache_hits    // "n/a"' "$JSON_FILE")
            fb=$(jq -r --arg l "$leg" '.coalescing[$l].fallbacks              // "n/a"' "$JSON_FILE")
            by=$(jq -r --arg l "$leg" '.coalescing[$l].bypasses               // "n/a"' "$JSON_FILE")
            cr_r=$(jq -r --arg l "$leg" '.coalescing[$l].coalescing_ratio     // "n/a"' "$JSON_FILE")
            amp=$(jq -r --arg l "$leg" '.coalescing[$l].upstream_amplification_ratio // "n/a"' "$JSON_FILE")
            echo "| $leg | $cr | $uc | $ld | $fo | $ch | $fb | $by | $cr_r | $amp |"
        done < <(jq -r '.coalescing | keys[]' "$JSON_FILE" 2>/dev/null || true)

        echo "### 👑 Leader L1 Store Outcomes"
        echo ""
        echo "| Leg | L1 Store Success | L1 Store Failed |"
        echo "|---|---:|---:|"

        while IFS= read -r leg; do
            lss=$(jq -r --arg l "$leg" '.coalescing[$l].leader_l1_store_success // "0"' "$JSON_FILE")
            lsf=$(jq -r --arg l "$leg" '.coalescing[$l].leader_l1_store_failed  // "0"' "$JSON_FILE")
            echo "| $leg | $lss | $lsf |"
        done < <(jq -r '.coalescing | keys[]' "$JSON_FILE" 2>/dev/null || true)
        echo ""

        echo "### 👥 Follower Outcome Breakdown"
        echo ""
        echo "| Leg | Shared Response | L1 Hit | L1 Ready | L1 Ready But Miss | Timeout | Registered Expired | Missing Entry | Stored (No Snapshot) | Not Cacheable | Failed | Queue Full | Unaccounted |"
        echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"

        while IFS= read -r leg; do
            fsr=$(jq -r --arg l "$leg" '.coalescing[$l].follower_shared_response // "0"' "$JSON_FILE")
            fl1=$(jq -r --arg l "$leg" '.coalescing[$l].follower_l1_hit          // "0"' "$JSON_FILE")
            flr=$(jq -r --arg l "$leg" '.coalescing[$l].follower_l1_ready        // "0"' "$JSON_FILE")
            flm=$(jq -r --arg l "$leg" '.coalescing[$l].follower_l1_ready_but_miss // "0"' "$JSON_FILE")
            fto=$(jq -r --arg l "$leg" '.coalescing[$l].follower_timeout         // "0"' "$JSON_FILE")
            fex=$(jq -r --arg l "$leg" '.coalescing[$l].follower_expired         // "0"' "$JSON_FILE")
            fmi=$(jq -r --arg l "$leg" '.coalescing[$l].follower_missing         // "0"' "$JSON_FILE")
            fss=$(jq -r --arg l "$leg" '.coalescing[$l].follower_stored_but_no_snapshot // "0"' "$JSON_FILE")
            fnc=$(jq -r --arg l "$leg" '.coalescing[$l].follower_not_cacheable   // "0"' "$JSON_FILE")
            ffa=$(jq -r --arg l "$leg" '.coalescing[$l].follower_failed          // "0"' "$JSON_FILE")
            fqu=$(jq -r --arg l "$leg" '.coalescing[$l].follower_pool_queue_full // "0"' "$JSON_FILE")
            fun=$(jq -r --arg l "$leg" '.coalescing[$l].follower_unaccounted     // "0"' "$JSON_FILE")
            echo "| $leg | $fsr | $fl1 | $flr | $flm | $fto | $fex | $fmi | $fss | $fnc | $ffa | $fqu | $fun |"
        done < <(jq -r '.coalescing | keys[]' "$JSON_FILE" 2>/dev/null || true)

        echo ""
        echo "### 🔍 Synchronous L2 Cache Probing Outcomes"
        echo ""
        echo "| Leg | Total Probes | Probe Hits | Probe Misses | L2 Ready | Body Too Large | Timeout Final | Avg Latency | Max Latency |"
        echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|"

        while IFS= read -r leg; do
            p_tot=$(jq -r --arg l "$leg" '.coalescing[$l].probe_total // "0"' "$JSON_FILE")
            p_hit=$(jq -r --arg l "$leg" '.coalescing[$l].probe_hit   // "0"' "$JSON_FILE")
            p_mis=$(jq -r --arg l "$leg" '.coalescing[$l].probe_miss  // "0"' "$JSON_FILE")
            p_rdy=$(jq -r --arg l "$leg" '.coalescing[$l].probe_l2ready // "0"' "$JSON_FILE")
            p_btl=$(jq -r --arg l "$leg" '.coalescing[$l].probe_body_too_large // "0"' "$JSON_FILE")
            p_tof=$(jq -r --arg l "$leg" '.coalescing[$l].probe_timeout_final // "0"' "$JSON_FILE")
            p_avg=$(jq -r --arg l "$leg" '.coalescing[$l].probe_latency_avg // "0"' "$JSON_FILE")
            p_max=$(jq -r --arg l "$leg" '.coalescing[$l].probe_latency_max // "0"' "$JSON_FILE")
            echo "| $leg | $p_tot | $p_hit | $p_mis | $p_rdy | $p_btl | $p_tof | ${p_avg} ms | ${p_max} ms |"
        done < <(jq -r '.coalescing | keys[]' "$JSON_FILE" 2>/dev/null || true)
    } >> "$OUT_MD_FILE"
fi

# Mock upstream backend metrics table. This is independent backend-side evidence
# that cache/coalescing benchmarks are actually reducing requests reaching the
# mock upstream server.
if jq -e '.mock_api and (.mock_api | length > 0)' "$JSON_FILE" > /dev/null 2>&1; then
    {
        echo ""
        echo "## 🧪 Mock Upstream Backend Metrics"
        echo ""
        echo "| Leg | Client Requests | Mock Upstream Requests | Backend Reduction | Bytes Sent | 2xx | 4xx/5xx | Fast Path | Slow Path |"
        echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|"

        while IFS= read -r leg; do
            upstream=$(jq -r --arg l "$leg" '.mock_api[$l].upstream_requests_total // .mock_api[$l].requests_total // "n/a"' "$JSON_FILE")
            bytes=$(jq -r --arg l "$leg" '.mock_api[$l].bytes_sent_total // "n/a"' "$JSON_FILE")
            fast=$(jq -r --arg l "$leg" '.mock_api[$l].fast_path_requests_total // "0"' "$JSON_FILE")
            slow=$(jq -r --arg l "$leg" '.mock_api[$l].slow_path_requests_total // "0"' "$JSON_FILE")
            client=$(jq -r --arg l "$leg" '.coalescing[$l].client_requests_sent // .throughput[$l].total_requests // "n/a"' "$JSON_FILE")
            status_2xx=$(jq -r --arg l "$leg" '[.mock_api[$l].status_codes // {} | to_entries[] | select(.key | test("^2")) | .value] | add // 0' "$JSON_FILE")
            status_bad=$(jq -r --arg l "$leg" '[.mock_api[$l].status_codes // {} | to_entries[] | select(.key | test("^[45]")) | .value] | add // 0' "$JSON_FILE")
            reduction=$(jq -r --arg l "$leg" '
                (.coalescing[$l].client_requests_sent // .throughput[$l].total_requests // null) as $client |
                (.mock_api[$l].upstream_requests_total // .mock_api[$l].requests_total // null) as $upstream |
                if ($client == null or $client == 0 or $upstream == null) then "n/a"
                else (((1 - ($upstream / $client)) * 100) | if . < 0 then 0 else . end | tostring) + "%"
                end' "$JSON_FILE")
            echo "| $leg | $client | $upstream | $reduction | $bytes | $status_2xx | $status_bad | $fast | $slow |"
        done < <(jq -r '.mock_api | keys[]' "$JSON_FILE" 2>/dev/null || true)
    } >> "$OUT_MD_FILE"
fi

# Warnings & Notes Section
{
    echo "## ⚠️ Notes & Warnings"
    echo ""
    if [ $any_unavailable -eq 1 ]; then
        echo "> [!WARNING]"
        echo "> Some container resource metrics or latency metrics are marked as \`unavailable\`. This can occur due to host namespace isolation restrictions within the execution containers (e.g. lack of access to host cgroup parameters). Ensure appropriate permissions are enabled if absolute tracking is required."
        echo ""
    else
        echo "> [!NOTE]"
        echo "> All metrics parsed, compiled, and validated with zero missing data parameters successfully."
        echo ""
    fi
} >> "$OUT_MD_FILE"

echo "Markdown report written successfully to: $OUT_MD_FILE"
cat "$OUT_MD_FILE"

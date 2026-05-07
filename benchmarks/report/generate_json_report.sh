#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

# Accept text report file as argument
TXT_FILE="${1:-}"

if [ -z "$TXT_FILE" ]; then
    echo "Usage: $0 <report_file.txt>" >&2
    exit 1
fi

if [ ! -f "$TXT_FILE" ]; then
    echo "ERROR: File '$TXT_FILE' not found" >&2
    exit 1
fi

echo "Compiling JSON report from text file: $TXT_FILE"

# Extract metadata
scenario=$(grep -E "^Scenario:" "$TXT_FILE" | head -n 1 | awk '{print $2}' || echo "unknown")
time_str=$(grep -E "^Time:" "$TXT_FILE" | head -n 1 | cut -d' ' -f2- || echo "unknown")
os_info=$(grep -E "^OS:" "$TXT_FILE" | head -n 1 | cut -d' ' -f2- || echo "unknown")
cpu_cores=$(grep -E "^CPU cores:" "$TXT_FILE" | head -n 1 | awk '{print $3}' || echo "unknown")
memory_total=$(grep -E "^Memory Total:" "$TXT_FILE" | head -n 1 | cut -d' ' -f3- || echo "unknown")
target_host=$(grep -E "^Target Host:" "$TXT_FILE" | head -n 1 | awk '{print $3}' || grep -E "^Target:" "$TXT_FILE" | head -n 1 | awk '{print $2}' || echo "unknown")

# Temporary files for collecting sections
LATENCY_JSON_MAP=$(mktemp)
THROUGHPUT_JSON_MAP=$(mktemp)
RESOURCES_JSON_MAP=$(mktemp)
PAYLOAD_JSON_MAP=$(mktemp)
COALESCING_JSON_MAP=$(mktemp)

echo "{}" > "$LATENCY_JSON_MAP"
echo "{}" > "$THROUGHPUT_JSON_MAP"
echo "{}" > "$RESOURCES_JSON_MAP"
echo "{}" > "$PAYLOAD_JSON_MAP"
echo "{}" > "$COALESCING_JSON_MAP"

# Helper to clean prefixes (e.g. "Leg 1 (Medium JSON)" -> "leg_1_medium_json" or just clean name "Leg 1")
clean_key() {
    local raw=$1
    local clean
    clean=$(echo "$raw" | sed -e 's/[[:space:]=:-]*$//' -e 's/^[[:space:]=:-]*//')
    echo "${clean:-main}"
}

# 1. Parse Latency JSON
while IFS= read -r line; do
    if [[ "$line" =~ ^(.*)Latency\ JSON:[[:space:]]*(.*)$ ]]; then
        key=$(clean_key "${BASH_REMATCH[1]}")
        val="${BASH_REMATCH[2]}"
        # Update JSON map using jq
        jq --arg k "$key" --argjson v "$val" '.[$k] = $v' "$LATENCY_JSON_MAP" > "${LATENCY_JSON_MAP}.tmp"
        mv "${LATENCY_JSON_MAP}.tmp" "$LATENCY_JSON_MAP"
    fi
done < <(grep -E "Latency JSON" "$TXT_FILE" || true)

# 2. Parse Throughput JSON
while IFS= read -r line; do
    if [[ "$line" =~ ^(.*)Throughput\ JSON:[[:space:]]*(.*)$ ]]; then
        key=$(clean_key "${BASH_REMATCH[1]}")
        val="${BASH_REMATCH[2]}"
        jq --arg k "$key" --argjson v "$val" '.[$k] = $v' "$THROUGHPUT_JSON_MAP" > "${THROUGHPUT_JSON_MAP}.tmp"
        mv "${THROUGHPUT_JSON_MAP}.tmp" "$THROUGHPUT_JSON_MAP"
    fi
done < <(grep -E "Throughput JSON" "$TXT_FILE" || true)

# 3. Parse Container Stats JSON
while IFS= read -r line; do
    if [[ "$line" =~ ^(.*)Container\ Stats\ JSON:[[:space:]]*(.*)$ ]]; then
        key=$(clean_key "${BASH_REMATCH[1]}")
        val="${BASH_REMATCH[2]}"
        jq --arg k "$key" --argjson v "$val" '.[$k] = $v' "$RESOURCES_JSON_MAP" > "${RESOURCES_JSON_MAP}.tmp"
        mv "${RESOURCES_JSON_MAP}.tmp" "$RESOURCES_JSON_MAP"
    fi
done < <(grep -E "Container Stats JSON" "$TXT_FILE" || true)

# 4. Parse Payload Savings JSON
while IFS= read -r line; do
    if [[ "$line" =~ ^(.*)Payload\ Savings\ JSON:[[:space:]]*(.*)$ ]]; then
        key=$(clean_key "${BASH_REMATCH[1]}")
        val="${BASH_REMATCH[2]}"
        jq --arg k "$key" --argjson v "$val" '.[$k] = $v' "$PAYLOAD_JSON_MAP" > "${PAYLOAD_JSON_MAP}.tmp"
        mv "${PAYLOAD_JSON_MAP}.tmp" "$PAYLOAD_JSON_MAP"
    fi
done < <(grep -E "Payload Savings JSON" "$TXT_FILE" || true)

# 5. Parse Coalescing JSON
while IFS= read -r line; do
    if [[ "$line" =~ ^(.*)Coalescing\ JSON:[[:space:]]*(.*)$ ]]; then
        key=$(clean_key "${BASH_REMATCH[1]}")
        val="${BASH_REMATCH[2]}"
        jq --arg k "$key" --argjson v "$val" '.[$k] = $v' "$COALESCING_JSON_MAP" > "${COALESCING_JSON_MAP}.tmp"
        mv "${COALESCING_JSON_MAP}.tmp" "$COALESCING_JSON_MAP"
    fi
done < <(grep -E "Coalescing JSON" "$TXT_FILE" || true)

# Build consolidated report JSON
OUT_JSON_FILE="${TXT_FILE%.txt}.json"

# Read maps
lat_data=$(cat "$LATENCY_JSON_MAP")
tp_data=$(cat "$THROUGHPUT_JSON_MAP")
res_data=$(cat "$RESOURCES_JSON_MAP")
pay_data=$(cat "$PAYLOAD_JSON_MAP")
coal_data=$(cat "$COALESCING_JSON_MAP")

# Compile consolidated JSON
jq -n \
    --arg bv "1.0.0" \
    --arg sc "$scenario" \
    --arg ts "$time_str" \
    --argjson lat "$lat_data" \
    --argjson tp "$tp_data" \
    --argjson res "$res_data" \
    --argjson pay "$pay_data" \
    --argjson coal "$coal_data" \
    --arg os "$os_info" \
    --arg cpu "$cpu_cores" \
    --arg mem "$memory_total" \
    --arg host "$target_host" \
    '{
        benchmark_version: $bv,
        scenario: $sc,
        timestamp: $ts,
        latency_ms: $lat,
        throughput: $tp,
        resources: $res,
        payload: $pay,
        coalescing: $coal,
        features: {
            os_info: $os,
            cpu_cores: (try ($cpu | tonumber) catch $cpu),
            memory_total: $mem,
            target_host: $host
        }
    }' > "$OUT_JSON_FILE"

# Clean up temporary maps
rm -f "$LATENCY_JSON_MAP" "$THROUGHPUT_JSON_MAP" "$RESOURCES_JSON_MAP" "$PAYLOAD_JSON_MAP" "$COALESCING_JSON_MAP"

echo "JSON report written successfully to: $OUT_JSON_FILE"
cat "$OUT_JSON_FILE" | jq .

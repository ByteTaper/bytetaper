#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

mock_api_reset_metrics() {
    local host=$1
    if ! curl -s --fail "${host}/metrics/reset" > /dev/null; then
        curl -s --fail "${host}/reset-count" > /dev/null
    fi
}

mock_api_snapshot() {
    local host=$1
    if ! curl -s --fail "${host}/metrics.json"; then
        local calls
        calls=$(curl -s --fail "${host}/call-count")
        jq -c -n --argjson calls "${calls:-0}" '{
            service: "mock-api",
            upstream_requests_total: $calls,
            requests_total: $calls,
            responses_total: $calls,
            metrics_requests_total: 0,
            bytes_sent_total: 0,
            fast_path_requests_total: 0,
            slow_path_requests_total: 0,
            status_codes: {},
            methods: {},
            paths: {},
            fixtures_served_total: {}
        }'
    fi
}

mock_api_delta() {
    local before_json=$1
    local after_json=$2

    jq -c -n \
      --argjson before "$before_json" \
      --argjson after "$after_json" \
      '{
        service: "mock-api",
        upstream_requests_total: (($after.upstream_requests_total // 0) - ($before.upstream_requests_total // 0)),
        requests_total: (($after.requests_total // $after.upstream_requests_total // 0) - ($before.requests_total // $before.upstream_requests_total // 0)),
        responses_total: (($after.responses_total // 0) - ($before.responses_total // 0)),
        metrics_requests_total: (($after.metrics_requests_total // 0) - ($before.metrics_requests_total // 0)),
        bytes_sent_total: (($after.bytes_sent_total // 0) - ($before.bytes_sent_total // 0)),
        fast_path_requests_total: (($after.fast_path_requests_total // 0) - ($before.fast_path_requests_total // 0)),
        slow_path_requests_total: (($after.slow_path_requests_total // 0) - ($before.slow_path_requests_total // 0)),
        status_codes: ($after.status_codes // {}),
        methods: ($after.methods // {}),
        paths: ($after.paths // {}),
        fixtures_served_total: ($after.fixtures_served_total // {})
      }'
}

mock_api_upstream_requests() {
    local metrics_json=$1
    jq -r '.upstream_requests_total // .requests_total // 0' <<< "$metrics_json"
}

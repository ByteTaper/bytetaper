#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

set -euo pipefail

assert_contains() {
    local file=$1
    local term=$2
    local msg=$3
    if ! grep -qF "$term" "$file"; then
        echo "FAIL: $msg" >&2
        echo "  File did not contain term: '$term'" >&2
        exit 1
    fi
    echo "PASS: $msg"
}

echo "=== Running Markdown Report Compiler Unit Tests ==="

MOCK_JSON=$(mktemp)
MOCK_MD="${MOCK_JSON%.json}.md"

cat << 'EOF' > "$MOCK_JSON"
{
  "benchmark_version": "1.0.0",
  "scenario": "unit_test_markdown",
  "timestamp": "Wed May  6 14:17:16 UTC 2026",
  "latency_ms": {
    "Leg A": {
      "latency_ms": {
        "p50": 10.5,
        "p95": 20.0,
        "p99": 50.0
      }
    }
  },
  "throughput": {
    "Leg A": {
      "throughput": {
        "requests_per_second": 120.5,
        "total_requests": 1200,
        "successful_requests": 1200,
        "failed_requests": 0
      },
      "total_requests": 1200,
      "successful_requests": 1200,
      "failed_requests": 0
    }
  },
  "resources": {
    "Leg A": {
      "envoy": {
        "cpu_percent": 5.5,
        "peak_memory_mb": 12.5
      },
      "bytetaper-extproc": {
        "cpu_percent": 8.2,
        "peak_memory_mb": 18.4
      },
      "mock-api": {
        "cpu_percent": 2.1,
        "peak_memory_mb": 9.0
      }
    }
  },
  "payload": {
    "Leg A": {
      "original_bytes_avg": 2048,
      "optimized_bytes_avg": 1024,
      "bytes_saved_avg": 1024,
      "reduction_ratio": "50.00%"
    }
  },
  "mock_api": {
    "Leg A": {
      "service": "mock-api",
      "upstream_requests_total": 1,
      "requests_total": 1,
      "responses_total": 1,
      "metrics_requests_total": 2,
      "bytes_sent_total": 2048,
      "fast_path_requests_total": 1,
      "slow_path_requests_total": 0,
      "status_codes": {
        "200": 1
      },
      "methods": {
        "GET": 1
      },
      "paths": {
        "/products/fast/123": 1
      }
    }
  },
  "features": {
    "os_info": "Linux test-host aarch64 Linux",
    "cpu_cores": 4,
    "memory_total": "8192 MB",
    "target_host": "http://envoy-unit-test:10000"
  }
}
EOF

./benchmarks/report/generate_markdown_report.sh "$MOCK_JSON"

if [ ! -f "$MOCK_MD" ]; then
    echo "FAIL: Output MD report file not created" >&2
    exit 1
fi

assert_contains "$MOCK_MD" "# 🚀 ByteTaper Performance Report" "Verify report title is present"
assert_contains "$MOCK_MD" "| **Scenario** | \`unit_test_markdown\` |" "Verify scenario name metadata in table"
assert_contains "$MOCK_MD" "| **Throughput (RPS)** | \`120.5 req/s\` |" "Verify mapped throughput RPS"
assert_contains "$MOCK_MD" "| **p50 Latency** | \`10.5 ms\` |" "Verify mapped latency percentiles"
assert_contains "$MOCK_MD" "| **Payload Reduction Ratio** | **\`50.00%\`** |" "Verify payload reduction styling"
assert_contains "$MOCK_MD" "| **bytetaper-extproc** | \`8.2%\` | \`18.4 MB\` |" "Verify container resources table columns mapping"
assert_contains "$MOCK_MD" "## 🧪 Mock Upstream Backend Metrics" "Verify mock upstream metrics section"
assert_contains "$MOCK_MD" "| Leg A | 1200 | 1 |" "Verify mock upstream metrics row"
assert_contains "$MOCK_MD" "> [!NOTE]" "Verify success note when all parameters are present"

rm -f "$MOCK_JSON" "$MOCK_MD"

echo "=== All Markdown Report Compiler Unit Tests Passed ==="
exit 0

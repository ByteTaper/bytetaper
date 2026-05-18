#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Haluan Irsad
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

import time
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

HOST = "0.0.0.0"
PORT = 8080

# Determine fixtures directory
FIXTURES_DIR = os.environ.get("BYTETAPER_FIXTURES_DIR", "/workspace/benchmarks/fixtures")
if not os.path.isdir(FIXTURES_DIR):
    FIXTURES_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "benchmarks", "fixtures"))

# Cache of fixtures loaded at startup
FIXTURES = {}
for name in ["small-json", "medium-json", "large-json", "huge-json", "products-by-id", "orders-list"]:
    path = os.path.join(FIXTURES_DIR, f"{name}.json")
    try:
        with open(path, "rb") as f:
            FIXTURES[name] = f.read()
    except Exception as e:
        print(f"Warning: Failed to load fixture {name} from {path}: {e}")
        FIXTURES[name] = b'{"error":"fixture_not_found"}'

CONTROL_PATHS = {
    "/call-count",
    "/reset-count",
    "/metrics",
    "/metrics.json",
    "/metrics/reset",
}

METRICS_LOCK = threading.Lock()
METRICS = {}


def _empty_metrics():
    return {
        "service": "mock-api",
        "upstream_requests_total": 0,
        "responses_total": 0,
        "metrics_requests_total": 0,
        "bytes_sent_total": 0,
        "fast_path_requests_total": 0,
        "slow_path_requests_total": 0,
        "status_codes": {},
        "methods": {},
        "paths": {},
        "fixtures_served_total": {},
        "started_at_epoch_ms": int(time.time() * 1000),
    }


def reset_metrics():
    with METRICS_LOCK:
        METRICS.clear()
        METRICS.update(_empty_metrics())


def record_control_request():
    with METRICS_LOCK:
        METRICS["metrics_requests_total"] = METRICS.get("metrics_requests_total", 0) + 1


def record_upstream_response(path, method, status_code, bytes_sent, delay_kind=None, fixture_name=None):
    # This is the benchmark-facing source of truth for actual backend fan-out.
    # Control endpoints are intentionally excluded so metrics polling does not
    # inflate upstream request counts.
    if path in CONTROL_PATHS:
        return

    status_key = str(status_code)
    with METRICS_LOCK:
        METRICS["upstream_requests_total"] = METRICS.get("upstream_requests_total", 0) + 1
        METRICS["responses_total"] = METRICS.get("responses_total", 0) + 1
        METRICS["bytes_sent_total"] = METRICS.get("bytes_sent_total", 0) + int(bytes_sent)
        METRICS["methods"][method] = METRICS["methods"].get(method, 0) + 1
        METRICS["paths"][path] = METRICS["paths"].get(path, 0) + 1
        METRICS["status_codes"][status_key] = METRICS["status_codes"].get(status_key, 0) + 1
        if delay_kind == "fast":
            METRICS["fast_path_requests_total"] = METRICS.get("fast_path_requests_total", 0) + 1
        elif delay_kind == "slow":
            METRICS["slow_path_requests_total"] = METRICS.get("slow_path_requests_total", 0) + 1
        if fixture_name is not None:
            fixtures = METRICS["fixtures_served_total"]
            fixtures[fixture_name] = fixtures.get(fixture_name, 0) + 1


def metrics_snapshot():
    with METRICS_LOCK:
        snapshot = json.loads(json.dumps(METRICS, sort_keys=True))
    snapshot["requests_total"] = snapshot.get("upstream_requests_total", 0)
    return snapshot


def render_prometheus_metrics(snapshot):
    lines = [
        "# HELP mock_api_upstream_requests_total Total benchmark-relevant requests that reached the mock upstream backend.",
        "# TYPE mock_api_upstream_requests_total counter",
        f"mock_api_upstream_requests_total {snapshot.get('upstream_requests_total', 0)}",
        "# HELP mock_api_responses_total Total benchmark-relevant responses sent by the mock upstream backend.",
        "# TYPE mock_api_responses_total counter",
        f"mock_api_responses_total {snapshot.get('responses_total', 0)}",
        "# HELP mock_api_metrics_requests_total Total control/metrics requests received by the mock API.",
        "# TYPE mock_api_metrics_requests_total counter",
        f"mock_api_metrics_requests_total {snapshot.get('metrics_requests_total', 0)}",
        "# HELP mock_api_bytes_sent_total Total response bytes sent for benchmark-relevant requests.",
        "# TYPE mock_api_bytes_sent_total counter",
        f"mock_api_bytes_sent_total {snapshot.get('bytes_sent_total', 0)}",
        "# HELP mock_api_fast_path_requests_total Total benchmark requests hitting paths that include /fast/.",
        "# TYPE mock_api_fast_path_requests_total counter",
        f"mock_api_fast_path_requests_total {snapshot.get('fast_path_requests_total', 0)}",
        "# HELP mock_api_slow_path_requests_total Total benchmark requests hitting paths that include /slow/.",
        "# TYPE mock_api_slow_path_requests_total counter",
        f"mock_api_slow_path_requests_total {snapshot.get('slow_path_requests_total', 0)}",
    ]

    for code, value in sorted(snapshot.get("status_codes", {}).items()):
        safe_code = str(code).replace('"', '\\"')
        lines.append(f'mock_api_status_code_total{{code="{safe_code}"}} {value}')

    for method, value in sorted(snapshot.get("methods", {}).items()):
        safe_method = str(method).replace('"', '\\"')
        lines.append(f'mock_api_method_requests_total{{method="{safe_method}"}} {value}')

    for path, value in sorted(snapshot.get("paths", {}).items()):
        safe_path = str(path).replace('"', '\\"')
        lines.append(f'mock_api_path_requests_total{{path="{safe_path}"}} {value}')

    return ("\n".join(lines) + "\n").encode("utf-8")


def build_payload(size=1024, scenario="default", sentinel="bt-001", version=1):
    base_object = {
        "service": "mock-api",
        "scenario": scenario,
        "sentinel": sentinel,
        "version": version,
        "payload": "x" * size,
        "secret_token": "super-secret-token-123",
        "debug_internal": "internal-debug-log-data",
        "request_path": "/api/v1/small",
        "generated_at_epoch_ms": 1716035000000,
    }
    return json.dumps(base_object, separators=(",", ":"), sort_keys=True).encode("utf-8")


DEFAULT_PAYLOAD = build_payload(1024)
reset_metrics()


class Handler(BaseHTTPRequestHandler):
    def do_HEAD(self) -> None:
        self.do_GET()

    def do_POST(self) -> None:
        self.do_GET()

    def write_response(self, status_code, body, content_type="application/json", extra_headers=None,
                       delay_kind=None, fixture_name=None):
        path = urlsplit(self.path).path
        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        if extra_headers:
            for key, value in extra_headers.items():
                self.send_header(key, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        record_upstream_response(path, self.command, status_code, len(body), delay_kind, fixture_name)

    def do_GET(self) -> None:
        path = urlsplit(self.path).path

        if path == "/call-count":
            record_control_request()
            body = str(metrics_snapshot().get("upstream_requests_total", 0)).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if path == "/reset-count" or path == "/metrics/reset":
            reset_metrics()
            body = b"ok"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if path == "/metrics.json":
            record_control_request()
            body = json.dumps(metrics_snapshot(), separators=(",", ":"), sort_keys=True).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if path == "/metrics":
            record_control_request()
            body = render_prometheus_metrics(metrics_snapshot())
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; version=0.0.4")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        # Artificial delay for load testing coalescing
        delay_kind = None
        if "/slow/" in path:
            delay_kind = "slow"
            time.sleep(0.1) # 100ms delay to trigger wait window timeout (50ms)
        elif "/fast/" in path:
            delay_kind = "fast"
            time.sleep(0.02) # 20ms delay to ensure concurrent requests overlap in ext-proc

        if path == "/orders":
            query = urlsplit(self.path).query
            params = {k: v for k, v in [q.split("=") for q in query.split("&") if "=" in q]}
            limit = params.get("limit", None)

            try:
                data_dict = json.loads(FIXTURES["orders-list"])
            except Exception:
                data_dict = {"data": []}
            data_dict["received_limit"] = limit

            response_body = json.dumps(data_dict, separators=(",", ":"), sort_keys=True).encode("utf-8")
            self.write_response(200, response_body, fixture_name="orders-list")
            return

        # Serve benchmark endpoints
        if path == "/small-json":
            self.write_response(200, FIXTURES["small-json"], fixture_name="small-json")
            return

        if path == "/medium-json":
            self.write_response(200, FIXTURES["medium-json"], fixture_name="medium-json")
            return

        if path == "/large-json":
            self.write_response(200, FIXTURES["large-json"], fixture_name="large-json")
            return

        if path == "/huge-json":
            self.write_response(200, FIXTURES["huge-json"], fixture_name="huge-json")
            return

        if path.startswith("/products/tier-l1/"):
            self.write_response(200, FIXTURES["small-json"], delay_kind=delay_kind,
                                fixture_name="small-json")
            return

        if path.startswith("/products/tier-l2complete/"):
            self.write_response(200, FIXTURES["medium-json"], delay_kind=delay_kind,
                                fixture_name="medium-json")
            return

        if path.startswith("/products/tier-l2warmonly/"):
            self.write_response(200, FIXTURES["large-json"], delay_kind=delay_kind,
                                fixture_name="large-json")
            return

        if path.startswith("/products/"):
            self.write_response(200, FIXTURES["products-by-id"], delay_kind=delay_kind,
                                fixture_name="products-by-id")
            return

        if path == "/api/v1/oversized":
            # Exact size matching logic from oversized_envoy_assert.py
            base_object = {
                "service": "mock-api",
                "scenario": "oversized-json",
                "sentinel": "bt-001",
                "version": 1,
                "payload": "",
            }
            base_body = json.dumps(base_object, separators=(",", ":"), sort_keys=True)
            base_size = len(base_body.encode("utf-8"))
            filler_length = 1200000 - base_size
            base_object["payload"] = "x" * filler_length
            payload = json.dumps(base_object, separators=(",", ":"), sort_keys=True).encode("utf-8")
            self.write_response(200, payload, delay_kind=delay_kind)
            return

        if path == "/api/v1/large":
            payload = build_payload(2048, scenario="large-json")
            self.write_response(200, payload, delay_kind=delay_kind)
            return

        if path == "/api/v1/small":
            payload = build_payload(512, scenario="small-json")
            self.write_response(200, payload, delay_kind=delay_kind)
            return

        if path == "/api/v1/already-encoded":
            payload = b"fake-gzip-data"
            self.write_response(200, payload, extra_headers={"Content-Encoding": "gzip"},
                                delay_kind=delay_kind)
            return

        if path.startswith("/api/v1/"):
            self.write_response(200, DEFAULT_PAYLOAD, delay_kind=delay_kind)
            return

        self.write_response(404, b'{"error":"not_found"}', delay_kind=delay_kind)

    def log_message(self, fmt: str, *args) -> None:
        return


if __name__ == "__main__":
    ThreadingHTTPServer.request_queue_size = 1024
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Mock API listening on {HOST}:{PORT}")
    server.serve_forever()

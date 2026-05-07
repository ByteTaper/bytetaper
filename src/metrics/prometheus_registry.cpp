// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "metrics/prometheus_registry.h"

namespace bytetaper::metrics {

void record_stream(MetricsRegistry* registry, const StreamRecord& record) {
    if (registry == nullptr) {
        return;
    }
    registry->streams_total.fetch_add(1, std::memory_order_relaxed);
    registry->bytes_original_total.fetch_add(record.original_bytes, std::memory_order_relaxed);
    registry->bytes_optimized_total.fetch_add(record.optimized_bytes, std::memory_order_relaxed);
    registry->fields_removed_total.fetch_add(record.removed_fields, std::memory_order_relaxed);
    if (record.original_bytes > record.optimized_bytes) {
        registry->bytes_saved_total.fetch_add(record.original_bytes - record.optimized_bytes,
                                              std::memory_order_relaxed);
    }
    if (record.transform_applied) {
        registry->transform_applied_total.fetch_add(1, std::memory_order_relaxed);
    }
    if (record.transform_error) {
        registry->transform_errors_total.fetch_add(1, std::memory_order_relaxed);
    }

    if (record.original_bytes > record.optimized_bytes && !record.route_id.empty()) {
        const std::uint64_t saved = record.original_bytes - record.optimized_bytes;
        RouteWasteMetric* target = nullptr;
        {
            std::lock_guard<std::mutex> lock(registry->routes_mutex);
            for (const auto& rm : registry->route_metrics) {
                if (rm->route_id == record.route_id) {
                    target = rm.get();
                    break;
                }
            }
            if (target == nullptr) {
                registry->route_metrics.push_back(
                    std::make_unique<RouteWasteMetric>(record.route_id));
                target = registry->route_metrics.back().get();
            }
        }
        target->waste_saved_bytes.fetch_add(saved, std::memory_order_relaxed);
    }
}

std::string render_prometheus_text(const MetricsRegistry& registry) {
    std::string out;
    out.reserve(1024);

    out += "# HELP bytetaper_streams_total Total gRPC streams processed\n";
    out += "# TYPE bytetaper_streams_total counter\n";
    out += "bytetaper_streams_total " +
           std::to_string(registry.streams_total.load(std::memory_order_relaxed)) + "\n";

    out += "# HELP bytetaper_bytes_original_total Total original response bytes\n";
    out += "# TYPE bytetaper_bytes_original_total counter\n";
    out += "bytetaper_bytes_original_total " +
           std::to_string(registry.bytes_original_total.load(std::memory_order_relaxed)) + "\n";

    out += "# HELP bytetaper_bytes_optimized_total Total optimized response bytes\n";
    out += "# TYPE bytetaper_bytes_optimized_total counter\n";
    out += "bytetaper_bytes_optimized_total " +
           std::to_string(registry.bytes_optimized_total.load(std::memory_order_relaxed)) + "\n";

    out += "# HELP bytetaper_bytes_saved_total Total bytes saved by processing\n";
    out += "# TYPE bytetaper_bytes_saved_total counter\n";
    out += "bytetaper_bytes_saved_total " +
           std::to_string(registry.bytes_saved_total.load(std::memory_order_relaxed)) + "\n";

    out += "# HELP bytetaper_fields_removed_total Total JSON fields removed by filtering\n";
    out += "# TYPE bytetaper_fields_removed_total counter\n";
    out += "bytetaper_fields_removed_total " +
           std::to_string(registry.fields_removed_total.load(std::memory_order_relaxed)) + "\n";

    out += "# HELP bytetaper_transform_applied_total Streams where transform was applied\n";
    out += "# TYPE bytetaper_transform_applied_total counter\n";
    out += "bytetaper_transform_applied_total " +
           std::to_string(registry.transform_applied_total.load(std::memory_order_relaxed)) + "\n";

    out += "# HELP bytetaper_transform_errors_total Total transform processing errors\n";
    out += "# TYPE bytetaper_transform_errors_total counter\n";
    out += "bytetaper_transform_errors_total " +
           std::to_string(registry.transform_errors_total.load(std::memory_order_relaxed)) + "\n";

    double reduction_ratio = 0.0;
    const std::uint64_t original = registry.bytes_original_total.load(std::memory_order_relaxed);
    const std::uint64_t optimized = registry.bytes_optimized_total.load(std::memory_order_relaxed);
    if (original > 0) {
        const double diff = static_cast<double>(original) - static_cast<double>(optimized);
        reduction_ratio = diff / static_cast<double>(original);
    }

    out += "# HELP bytetaper_payload_reduction_ratio Ratio of bytes saved vs original\n";
    out += "# TYPE bytetaper_payload_reduction_ratio gauge\n";
    out += "bytetaper_payload_reduction_ratio " + std::to_string(reduction_ratio) + "\n";

    out += "# HELP bytetaper_route_waste_saved_bytes_total Bytes saved per route\n";
    out += "# TYPE bytetaper_route_waste_saved_bytes_total counter\n";
    {
        std::lock_guard<std::mutex> lock(registry.routes_mutex);
        for (const auto& rm : registry.route_metrics) {
            out += "bytetaper_route_waste_saved_bytes_total{route=\"" + rm->route_id + "\"} " +
                   std::to_string(rm->waste_saved_bytes.load(std::memory_order_relaxed)) + "\n";
        }
    }

    char component_buf[4096];
    if (render_pagination_metrics_prometheus(registry.pagination_metrics, component_buf,
                                             sizeof(component_buf)) > 0) {
        out += component_buf;
    }
    if (render_cache_metrics_prometheus(registry.cache_metrics, component_buf,
                                        sizeof(component_buf)) > 0) {
        out += component_buf;
    }
    if (render_compression_metrics_prometheus(registry.compression_metrics, component_buf,
                                              sizeof(component_buf)) > 0) {
        out += component_buf;
    }
    if (render_coalescing_metrics_prometheus(registry.coalescing_metrics, component_buf,
                                             sizeof(component_buf)) > 0) {
        out += component_buf;
    }
    if (render_runtime_metrics_prometheus(registry.runtime_metrics, component_buf,
                                          sizeof(component_buf)) > 0) {
        out += component_buf;
    }

    return out;
}

} // namespace bytetaper::metrics

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "observability/trace.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace bytetaper::observability {

static TraceConfig g_trace_config{};
static TraceRingBuffer g_trace_ring{};

static std::atomic<std::uint32_t> g_trace_counter{ 1 };
static std::atomic<std::uint32_t> g_span_counter{ 1 };
static std::atomic<std::uint64_t> g_sample_counter{ 0 };

static std::uint64_t current_unix_nano() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

const char* trace_latency_class_str(TraceLatencyClass c) {
    switch (c) {
    case TraceLatencyClass::GrpcReadWait:
        return "grpc_read_wait";
    case TraceLatencyClass::ActiveProcessing:
        return "active_processing";
    case TraceLatencyClass::ActiveProcessingDetail:
        return "active_processing_detail";
    case TraceLatencyClass::GrpcWrite:
        return "grpc_write";
    case TraceLatencyClass::RuntimeQueueWait:
        return "runtime_queue_wait";
    case TraceLatencyClass::CacheIo:
        return "cache_io";
    case TraceLatencyClass::UpstreamWait:
        return "upstream_wait";
    case TraceLatencyClass::Mixed:
        return "mixed";
    default:
        return "unknown";
    }
}

TraceConfig trace_config_from_env() {
    TraceConfig config{};
    const char* enabled_env = std::getenv("BYTETAPER_TRACE_ENABLED");
    if (enabled_env != nullptr && std::strcmp(enabled_env, "true") == 0) {
        config.enabled = true;
    } else {
        config.enabled = false;
    }

    const char* mode_env = std::getenv("BYTETAPER_TRACE_MODE");
    if (mode_env != nullptr) {
        std::strncpy(config.mode, mode_env, sizeof(config.mode) - 1);
        config.mode[sizeof(config.mode) - 1] = '\0';
        if (std::strcmp(config.mode, "off") == 0) {
            config.enabled = false;
        }
    } else {
        std::strcpy(config.mode, "slow");
    }

    const char* out_dir_env = std::getenv("BYTETAPER_TRACE_OUTPUT_DIR");
    if (out_dir_env != nullptr) {
        std::strncpy(config.output_dir, out_dir_env, sizeof(config.output_dir) - 1);
        config.output_dir[sizeof(config.output_dir) - 1] = '\0';
    }

    const char* slow_total_env = std::getenv("BYTETAPER_TRACE_SLOW_TOTAL_MS");
    if (slow_total_env == nullptr) {
        slow_total_env = std::getenv("BYTETAPER_TRACE_SLOW_MS");
    }
    if (slow_total_env != nullptr) {
        config.slow_total_ms = std::strtoul(slow_total_env, nullptr, 10);
    }

    const char* slow_phase_env = std::getenv("BYTETAPER_TRACE_SLOW_PHASE_MS");
    if (slow_phase_env == nullptr) {
        slow_phase_env = std::getenv("BYTETAPER_TRACE_PHASE_SLOW_MS");
    }
    if (slow_phase_env != nullptr) {
        config.slow_phase_ms = std::strtoul(slow_phase_env, nullptr, 10);
    }

    const char* sample_rate_env = std::getenv("BYTETAPER_TRACE_SAMPLE_RATE");
    if (sample_rate_env != nullptr) {
        double val = std::strtod(sample_rate_env, nullptr);
        config.sample_rate_ppm = static_cast<std::uint32_t>(val * 1000000.0);
    }

    const char* max_records_env = std::getenv("BYTETAPER_TRACE_MAX_RECORDS");
    if (max_records_env != nullptr) {
        config.max_records = std::strtoul(max_records_env, nullptr, 10);
        if (config.max_records > 4096)
            config.max_records = 4096;
    }

    return config;
}

void trace_init(const TraceConfig& config) {
    g_trace_config = config;
    g_trace_ring.write_index.store(0, std::memory_order_relaxed);
    g_trace_ring.dropped_count.store(0, std::memory_order_relaxed);
    std::uint32_t limit = config.max_records;
    if (limit == 0 || limit > 4096)
        limit = 4096;
    for (std::size_t i = 0; i < limit; ++i) {
        g_trace_ring.records[i] = TraceRecord{};
    }
    for (std::size_t i = 0; i < kMaxTracedRoutes; ++i) {
        g_trace_ring.route_quotas[i].route_id[0] = '\0';
        g_trace_ring.route_quotas[i].count.store(0, std::memory_order_relaxed);
    }
}

const TraceConfig& trace_global_config() {
    return g_trace_config;
}

TraceRingBuffer* trace_global_ring() {
    return &g_trace_ring;
}

void trace_start_record(TraceRecord* record, const char* scenario) {
    if (record == nullptr)
        return;
    *record = TraceRecord{};
    std::uint32_t tid = g_trace_counter.fetch_add(1, std::memory_order_relaxed);
    std::snprintf(record->trace_id.value, sizeof(record->trace_id.value), "bt-%08u", tid);
    if (scenario != nullptr) {
        std::strncpy(record->scenario, scenario, sizeof(record->scenario) - 1);
    }

    auto root_scope =
        trace_start_span(record, "bytetaper.request", TraceLatencyClass::Unknown, nullptr);
    if (root_scope.span != nullptr) {
        record->root_span_id = root_scope.span->span_id;
    }
}

void trace_set_route(TraceRecord* record, const char* route_id, const char* request_path) {
    if (record == nullptr)
        return;
    if (route_id != nullptr) {
        std::strncpy(record->route_id, route_id, sizeof(record->route_id) - 1);
    }
    if (request_path != nullptr) {
        std::strncpy(record->request_path, request_path, sizeof(record->request_path) - 1);
    }
}

void trace_set_compression_info(TraceRecord* record, bool candidate, bool decision_final,
                                std::size_t body_len, bool body_size_known) {
    if (record == nullptr)
        return;
    record->compression_candidate = candidate;
    record->compression_decision_final = decision_final;
    record->response_body_len = body_len;
    record->response_body_size_known = body_size_known;
}

void trace_set_cache_info(TraceRecord* record, bool cache_hit) {
    if (record == nullptr)
        return;
    record->cache_hit = cache_hit;
}

void trace_set_coalescing_info(TraceRecord* record, bool leader, bool follower) {
    if (record == nullptr)
        return;
    record->coalescing_leader = leader;
    record->coalescing_follower = follower;
}

void trace_set_coalescing_context(TraceRecord* record, const char* group_id, const char* key_hash,
                                  const char* role, const char* decision, const char* attach_result,
                                  const char* attach_failure_reason, const char* wakeup_reason,
                                  const char* result_source, const char* upstream_call_reason,
                                  std::uint64_t lifecycle_generation,
                                  const char* leader_request_id) {
    if (record == nullptr)
        return;
    record->coalescing_has_context = true;
    if (group_id != nullptr) {
        std::strncpy(record->coalescing_group_id, group_id,
                     sizeof(record->coalescing_group_id) - 1);
        record->coalescing_group_id[sizeof(record->coalescing_group_id) - 1] = '\0';
    }
    if (key_hash != nullptr) {
        std::strncpy(record->coalescing_key_hash, key_hash,
                     sizeof(record->coalescing_key_hash) - 1);
        record->coalescing_key_hash[sizeof(record->coalescing_key_hash) - 1] = '\0';
    }
    if (role != nullptr) {
        std::strncpy(record->coalescing_role, role, sizeof(record->coalescing_role) - 1);
        record->coalescing_role[sizeof(record->coalescing_role) - 1] = '\0';
    }
    if (decision != nullptr) {
        std::strncpy(record->coalescing_decision, decision,
                     sizeof(record->coalescing_decision) - 1);
        record->coalescing_decision[sizeof(record->coalescing_decision) - 1] = '\0';
    }
    if (attach_result != nullptr) {
        std::strncpy(record->coalescing_attach_result, attach_result,
                     sizeof(record->coalescing_attach_result) - 1);
        record->coalescing_attach_result[sizeof(record->coalescing_attach_result) - 1] = '\0';
    }
    if (attach_failure_reason != nullptr) {
        std::strncpy(record->coalescing_attach_failure_reason, attach_failure_reason,
                     sizeof(record->coalescing_attach_failure_reason) - 1);
        record->coalescing_attach_failure_reason[sizeof(record->coalescing_attach_failure_reason) -
                                                 1] = '\0';
    }
    if (wakeup_reason != nullptr) {
        std::strncpy(record->coalescing_wakeup_reason, wakeup_reason,
                     sizeof(record->coalescing_wakeup_reason) - 1);
        record->coalescing_wakeup_reason[sizeof(record->coalescing_wakeup_reason) - 1] = '\0';
    }
    if (result_source != nullptr) {
        std::strncpy(record->coalescing_result_source, result_source,
                     sizeof(record->coalescing_result_source) - 1);
        record->coalescing_result_source[sizeof(record->coalescing_result_source) - 1] = '\0';
    }
    if (upstream_call_reason != nullptr) {
        std::strncpy(record->coalescing_upstream_call_reason, upstream_call_reason,
                     sizeof(record->coalescing_upstream_call_reason) - 1);
        record
            ->coalescing_upstream_call_reason[sizeof(record->coalescing_upstream_call_reason) - 1] =
            '\0';
    }
    record->coalescing_lifecycle_generation = lifecycle_generation;
    if (leader_request_id != nullptr) {
        std::strncpy(record->coalescing_leader_request_id, leader_request_id,
                     sizeof(record->coalescing_leader_request_id) - 1);
        record->coalescing_leader_request_id[sizeof(record->coalescing_leader_request_id) - 1] =
            '\0';
    }
}

void trace_classify(TraceRecord* record) {
    if (record == nullptr)
        return;
    record->total_read_wait_nano = 0;
    record->total_active_processing_nano = 0;
    record->total_grpc_write_nano = 0;
    record->total_runtime_queue_wait_nano = 0;
    record->total_cache_io_nano = 0;

    for (std::size_t i = 0; i < record->span_count; ++i) {
        const TraceSpan& s = record->spans[i];
        switch (s.latency_class) {
        case TraceLatencyClass::GrpcReadWait:
            record->total_read_wait_nano += s.duration_nano;
            break;
        case TraceLatencyClass::ActiveProcessing:
            record->total_active_processing_nano += s.duration_nano;
            break;
        // ActiveProcessingDetail: intentionally excluded to avoid double-counting child spans
        case TraceLatencyClass::GrpcWrite:
            record->total_grpc_write_nano += s.duration_nano;
            break;
        case TraceLatencyClass::RuntimeQueueWait:
            record->total_runtime_queue_wait_nano += s.duration_nano;
            break;
        case TraceLatencyClass::CacheIo:
            record->total_cache_io_nano += s.duration_nano;
            break;
        default:
            break;
        }
    }

    const std::uint64_t total = record->total_duration_nano;
    if (total == 0) {
        record->dominant_latency_class = TraceLatencyClass::Unknown;
        return;
    }

    const auto pct = [total](std::uint64_t v) { return v * 100 / total; };

    if (pct(record->total_read_wait_nano) >= 60) {
        record->dominant_latency_class = TraceLatencyClass::GrpcReadWait;
    } else if (pct(record->total_grpc_write_nano) >= 60) {
        record->dominant_latency_class = TraceLatencyClass::GrpcWrite;
    } else if (pct(record->total_active_processing_nano) >= 60) {
        record->dominant_latency_class = TraceLatencyClass::ActiveProcessing;
    } else if (pct(record->total_runtime_queue_wait_nano) >= 60) {
        record->dominant_latency_class = TraceLatencyClass::RuntimeQueueWait;
    } else if (pct(record->total_cache_io_nano) >= 60) {
        record->dominant_latency_class = TraceLatencyClass::CacheIo;
    } else {
        record->dominant_latency_class = TraceLatencyClass::Mixed;
    }
}

bool trace_is_slow(const TraceRecord& record, const TraceConfig& config) {
    if (!config.enabled)
        return false;
    if (record.total_duration_nano >= config.slow_total_ms * 1000000ULL) {
        return true;
    }
    for (std::size_t i = 0; i < record.span_count; ++i) {
        const auto& span = record.spans[i];
        if (span.duration_nano >= config.slow_phase_ms * 1000000ULL) {
            return true;
        }
    }
    return false;
}

static std::uint64_t hash_u64(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

bool trace_is_sampled(const TraceConfig& config) {
    if (!config.enabled)
        return false;
    std::uint64_t val = g_sample_counter.fetch_add(1, std::memory_order_relaxed);
    return (hash_u64(val) % 1000000) < config.sample_rate_ppm;
}

TraceSpanScope trace_start_span(TraceRecord* record, const char* name,
                                TraceLatencyClass latency_class, const SpanId* parent_span_id) {
    if (record == nullptr || record->span_count >= kMaxSpansPerTrace) {
        return TraceSpanScope{ nullptr };
    }
    TraceSpan* span = &record->spans[record->span_count++];
    *span = TraceSpan{};

    std::uint32_t sid = g_span_counter.fetch_add(1, std::memory_order_relaxed);
    std::snprintf(span->span_id.value, sizeof(span->span_id.value), "%04u", sid);
    if (parent_span_id != nullptr) {
        span->parent_span_id = *parent_span_id;
    }
    if (name != nullptr) {
        std::strncpy(span->name, name, sizeof(span->name) - 1);
    }
    span->start_unix_nano = current_unix_nano();
    span->latency_class = latency_class;
    span->kind = TraceSpanKind::Internal;

    return TraceSpanScope{ span };
}

void TraceSpanScope::end() {
    if (span == nullptr || span->end_unix_nano != 0)
        return;
    span->end_unix_nano = current_unix_nano();
    if (span->end_unix_nano >= span->start_unix_nano) {
        span->duration_nano = span->end_unix_nano - span->start_unix_nano;
    } else {
        span->duration_nano = 0;
    }
}

void trace_rename_span(TraceSpan* span, const char* name) {
    if (span == nullptr || name == nullptr)
        return;
    std::strncpy(span->name, name, sizeof(span->name) - 1);
    span->name[sizeof(span->name) - 1] = '\0';
}

bool trace_push(TraceRingBuffer* ring, const TraceConfig& config, const TraceRecord& record) {
    if (ring == nullptr)
        return false;

    const char* r_id = record.route_id[0] != '\0' ? record.route_id : "default";

    // Find or create quota entry
    RouteTraceQuota* quota_entry = nullptr;
    for (std::size_t i = 0; i < kMaxTracedRoutes; ++i) {
        if (std::strcmp(ring->route_quotas[i].route_id, r_id) == 0) {
            quota_entry = &ring->route_quotas[i];
            break;
        }
    }

    if (quota_entry == nullptr) {
        for (std::size_t i = 0; i < kMaxTracedRoutes; ++i) {
            if (ring->route_quotas[i].route_id[0] == '\0') {
                static std::mutex g_quota_mutex;
                std::lock_guard<std::mutex> lock(g_quota_mutex);
                if (ring->route_quotas[i].route_id[0] == '\0') {
                    std::strncpy(ring->route_quotas[i].route_id, r_id,
                                 sizeof(ring->route_quotas[i].route_id) - 1);
                    quota_entry = &ring->route_quotas[i];
                    break;
                } else if (std::strcmp(ring->route_quotas[i].route_id, r_id) == 0) {
                    quota_entry = &ring->route_quotas[i];
                    break;
                }
            }
        }
    }

    if (quota_entry == nullptr) {
        quota_entry = &ring->route_quotas[0];
    }

    std::uint32_t current_count = quota_entry->count.load(std::memory_order_relaxed);
    if (current_count >= config.max_per_route) {
        ring->dropped_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    quota_entry->count.fetch_add(1, std::memory_order_relaxed);

    std::uint32_t max_rec = config.max_records;
    if (max_rec == 0 || max_rec > 4096)
        max_rec = 4096;

    std::uint32_t idx = ring->write_index.fetch_add(1, std::memory_order_relaxed);
    std::size_t ring_idx = idx % max_rec;

    const auto& old_record = ring->records[ring_idx];
    if (old_record.span_count > 0) {
        const char* old_r_id = old_record.route_id[0] != '\0' ? old_record.route_id : "default";
        for (std::size_t i = 0; i < kMaxTracedRoutes; ++i) {
            if (std::strcmp(ring->route_quotas[i].route_id, old_r_id) == 0) {
                if (ring->route_quotas[i].count.load(std::memory_order_relaxed) > 0) {
                    ring->route_quotas[i].count.fetch_sub(1, std::memory_order_relaxed);
                }
                break;
            }
        }
        ring->dropped_count.fetch_add(1, std::memory_order_relaxed);
    }

    ring->records[ring_idx] = record;
    return true;
}

std::size_t trace_format_jsonl(const TraceRecord& record, char* buf, std::size_t buf_size) {
    if (buf == nullptr || buf_size == 0)
        return 0;

    char spans_buf[8192];
    std::size_t spans_offset = 0;
    spans_buf[0] = '[';
    spans_offset += 1;

    for (std::size_t i = 0; i < record.span_count; ++i) {
        const auto& span = record.spans[i];
        char span_item[512];
        int len = std::snprintf(
            span_item, sizeof(span_item),
            "{\"span_id\":\"%s\",\"parent_span_id\":\"%s\",\"name\":\"%s\","
            "\"start_unix_nano\":%llu,\"end_unix_nano\":%llu,\"duration_nano\":"
            "%llu,\"latency_class\":\"%s\"}",
            span.span_id.value, span.parent_span_id.value, span.name,
            (unsigned long long) span.start_unix_nano, (unsigned long long) span.end_unix_nano,
            (unsigned long long) span.duration_nano, trace_latency_class_str(span.latency_class));

        if (len < 0 || spans_offset + len + 2 >= sizeof(spans_buf)) {
            break;
        }

        if (i > 0) {
            spans_buf[spans_offset] = ',';
            spans_offset += 1;
        }
        std::memcpy(spans_buf + spans_offset, span_item, len);
        spans_offset += len;
    }
    spans_buf[spans_offset] = ']';
    spans_buf[spans_offset + 1] = '\0';
    spans_offset += 1;

    char coalescing_buf[1024] = "";
    if (record.coalescing_has_context) {
        std::snprintf(
            coalescing_buf, sizeof(coalescing_buf),
            ",\"coalescing\":{"
            "\"group_id\":\"%s\",\"key_hash\":\"%s\",\"role\":\"%s\",\"decision\":\"%s\","
            "\"attach_result\":\"%s\",\"attach_failure_reason\":\"%s\",\"wakeup_reason\":\"%s\","
            "\"result_source\":\"%s\",\"upstream_call_reason\":\"%s\","
            "\"lifecycle_generation\":%llu,\"leader_request_id\":\"%s\"}",
            record.coalescing_group_id, record.coalescing_key_hash, record.coalescing_role,
            record.coalescing_decision, record.coalescing_attach_result,
            record.coalescing_attach_failure_reason, record.coalescing_wakeup_reason,
            record.coalescing_result_source, record.coalescing_upstream_call_reason,
            static_cast<unsigned long long>(record.coalescing_lifecycle_generation),
            record.coalescing_leader_request_id);
    }

    int total_len = std::snprintf(
        buf, buf_size,
        "{\"trace_id\":\"%s\",\"root_span_id\":\"%s\",\"scenario\":\"%s\","
        "\"route_id\":\"%s\",\"request_path\":\"%s\",\"compression_candidate\":%s,\"compression_"
        "decision_final\":%s,"
        "\"cache_hit\":%s,\"coalescing_leader\":%s,\"coalescing_follower\":%s,"
        "\"response_body_size_known\":%s,\"response_body_len\":%zu,\"response_status_code\":%u,"
        "\"total_duration_nano\":%llu,\"total_read_wait_nano\":%llu,\"total_active_processing_"
        "nano\":%llu,"
        "\"total_grpc_write_nano\":%llu,\"total_runtime_queue_wait_nano\":%llu,\"total_cache_io_"
        "nano\":%llu,"
        "\"dominant_latency_class\":\"%s\"%s,\"spans\":%s}\n",
        record.trace_id.value, record.root_span_id.value, record.scenario, record.route_id,
        record.request_path, record.compression_candidate ? "true" : "false",
        record.compression_decision_final ? "true" : "false", record.cache_hit ? "true" : "false",
        record.coalescing_leader ? "true" : "false", record.coalescing_follower ? "true" : "false",
        record.response_body_size_known ? "true" : "false", record.response_body_len,
        (unsigned int) record.response_status_code, (unsigned long long) record.total_duration_nano,
        (unsigned long long) record.total_read_wait_nano,
        (unsigned long long) record.total_active_processing_nano,
        (unsigned long long) record.total_grpc_write_nano,
        (unsigned long long) record.total_runtime_queue_wait_nano,
        (unsigned long long) record.total_cache_io_nano,
        trace_latency_class_str(record.dominant_latency_class), coalescing_buf, spans_buf);

    if (total_len < 0)
        return 0;
    if (static_cast<std::size_t>(total_len) >= buf_size) {
        return buf_size - 1;
    }
    return static_cast<std::size_t>(total_len);
}

struct SpanStat {
    char name[96];
    std::uint64_t duration_nano;
};

static int compare_span_stats(const void* a, const void* b) {
    const SpanStat* sa = static_cast<const SpanStat*>(a);
    const SpanStat* sb = static_cast<const SpanStat*>(b);
    if (sa->duration_nano > sb->duration_nano)
        return -1;
    if (sa->duration_nano < sb->duration_nano)
        return 1;
    return 0;
}

struct RouteStats {
    char route_id[64] = { 0 };
    std::size_t count = 0;
    std::uint64_t max_duration_nano = 0;
    std::size_t class_read_wait = 0;
    std::size_t class_active = 0;
    std::size_t class_write = 0;
    std::size_t class_queue = 0;
    std::size_t class_cache = 0;
    std::size_t class_mixed = 0;
    std::size_t class_unknown = 0;
};

void trace_flush(TraceRingBuffer* ring, const TraceConfig& config, const char* scenario) {
    if (ring == nullptr)
        return;
    std::uint32_t total_records = ring->write_index.load(std::memory_order_relaxed);
    if (total_records == 0)
        return;

    std::string out_dir = config.output_dir;
    mkdir(out_dir.c_str(), 0777);

    std::time_t rawtime = std::time(nullptr);
    struct std::tm timeinfo{};
    localtime_r(&rawtime, &timeinfo);
    char time_str[64];
    std::strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", &timeinfo);

    const char* scen = (scenario != nullptr && scenario[0] != '\0') ? scenario : "default";

    char jsonl_path[512];
    std::snprintf(jsonl_path, sizeof(jsonl_path), "%s/%s_%s.trace.jsonl", out_dir.c_str(), scen,
                  time_str);

    char md_path[512];
    std::snprintf(md_path, sizeof(md_path), "%s/%s_%s.summary.md", out_dir.c_str(), scen, time_str);

    std::FILE* jsonl_file = std::fopen(jsonl_path, "w");
    if (jsonl_file == nullptr)
        return;

    std::uint32_t max_rec = config.max_records;
    if (max_rec == 0 || max_rec > 4096)
        max_rec = 4096;

    std::uint32_t start_idx = (total_records > max_rec) ? (total_records - max_rec) : 0;
    std::uint32_t end_idx = total_records;

    std::vector<SpanStat> read_spans;
    std::vector<SpanStat> active_spans;
    std::vector<SpanStat> write_spans;
    std::vector<SpanStat> cache_spans;

    std::uint64_t sum_total_duration = 0;
    std::uint64_t max_total_duration = 0;
    std::size_t valid_records_count = 0;

    std::vector<RouteStats> route_breakdowns;

    char json_line[8192];
    for (std::uint32_t i = start_idx; i < end_idx; ++i) {
        std::size_t ring_idx = i % max_rec;
        const auto& record = ring->records[ring_idx];
        if (record.span_count > 0) {
            std::size_t len = trace_format_jsonl(record, json_line, sizeof(json_line));
            if (len > 0) {
                std::fwrite(json_line, 1, len, jsonl_file);
            }
            valid_records_count++;
            sum_total_duration += record.total_duration_nano;
            if (record.total_duration_nano > max_total_duration) {
                max_total_duration = record.total_duration_nano;
            }

            // Find or create route breakdown stats
            const char* r_id = record.route_id[0] != '\0' ? record.route_id : "default";
            RouteStats* r_stat = nullptr;
            for (auto& stat : route_breakdowns) {
                if (std::strcmp(stat.route_id, r_id) == 0) {
                    r_stat = &stat;
                    break;
                }
            }
            if (r_stat == nullptr) {
                RouteStats new_stat{};
                std::strncpy(new_stat.route_id, r_id, sizeof(new_stat.route_id) - 1);
                route_breakdowns.push_back(new_stat);
                r_stat = &route_breakdowns.back();
            }

            r_stat->count++;
            if (record.total_duration_nano > r_stat->max_duration_nano) {
                r_stat->max_duration_nano = record.total_duration_nano;
            }

            switch (record.dominant_latency_class) {
            case TraceLatencyClass::GrpcReadWait:
                r_stat->class_read_wait++;
                break;
            case TraceLatencyClass::ActiveProcessing:
                r_stat->class_active++;
                break;
            case TraceLatencyClass::GrpcWrite:
                r_stat->class_write++;
                break;
            case TraceLatencyClass::RuntimeQueueWait:
                r_stat->class_queue++;
                break;
            case TraceLatencyClass::CacheIo:
                r_stat->class_cache++;
                break;
            case TraceLatencyClass::Mixed:
                r_stat->class_mixed++;
                break;
            default:
                r_stat->class_unknown++;
                break;
            }

            for (std::size_t j = 0; j < record.span_count; ++j) {
                const auto& s = record.spans[j];
                SpanStat stat{};
                std::strncpy(stat.name, s.name, sizeof(stat.name) - 1);
                stat.name[sizeof(stat.name) - 1] = '\0';
                stat.duration_nano = s.duration_nano;

                if (s.latency_class == TraceLatencyClass::GrpcReadWait) {
                    read_spans.push_back(stat);
                } else if (s.latency_class == TraceLatencyClass::ActiveProcessing) {
                    active_spans.push_back(stat);
                } else if (s.latency_class == TraceLatencyClass::GrpcWrite) {
                    write_spans.push_back(stat);
                } else if (s.latency_class == TraceLatencyClass::CacheIo ||
                           s.latency_class == TraceLatencyClass::RuntimeQueueWait) {
                    cache_spans.push_back(stat);
                }
            }
        }
    }
    std::fclose(jsonl_file);

    if (valid_records_count == 0)
        return;

    std::FILE* md_file = std::fopen(md_path, "w");
    if (md_file != nullptr) {
        std::fprintf(md_file, "# ⏱️ ByteTaper Tail Observability Summary\n\n");
        std::fprintf(md_file, "| Parameter | Value |\n");
        std::fprintf(md_file, "| :--- | :--- |\n");
        std::fprintf(md_file, "| **Scenario** | `%s` |\n", scen);
        std::fprintf(md_file, "| **Timestamp** | `%s` |\n", time_str);
        std::fprintf(md_file, "| **Traced Slow Requests** | `%zu` |\n", valid_records_count);
        std::fprintf(md_file, "| **Ring Overwrite Drops** | `%llu` |\n",
                     (unsigned long long) ring->dropped_count.load());
        std::fprintf(md_file, "| **Average Request Duration** | `%.3f ms` |\n",
                     (sum_total_duration / (double) valid_records_count) / 1000000.0);
        std::fprintf(md_file, "| **Max Request Duration** | `%.3f ms` |\n",
                     max_total_duration / 1000000.0);
        std::fprintf(md_file, "\n");

        std::fprintf(md_file, "## 🚦 Per-Route Broken-down Quotas & Latency\n\n");
        std::fprintf(
            md_file,
            "| Route ID | Traced Records | Max Duration (ms) | Dominant Latency Distribution |\n");
        std::fprintf(md_file, "| :--- | :--- | :--- | :--- |\n");
        for (const auto& breakdown : route_breakdowns) {
            std::fprintf(md_file,
                         "| `%s` | `%zu` | `%.3f ms` | ReadWait: %zu, Active: %zu, Write: %zu, "
                         "Cache: %zu, Mixed: %zu |\n",
                         breakdown.route_id, breakdown.count,
                         breakdown.max_duration_nano / 1000000.0, breakdown.class_read_wait,
                         breakdown.class_active, breakdown.class_write, breakdown.class_cache,
                         breakdown.class_mixed);
        }
        std::fprintf(md_file, "\n");

        if (!read_spans.empty()) {
            std::fprintf(md_file, "## ⏱️ Top Read-Wait Spans\n\n");
            std::fprintf(md_file, "| Rank | Span Name | Duration (ms) |\n");
            std::fprintf(md_file, "| :--- | :--- | :--- |\n");
            std::qsort(read_spans.data(), read_spans.size(), sizeof(SpanStat), compare_span_stats);
            std::size_t count = std::min(read_spans.size(), static_cast<std::size_t>(5));
            for (std::size_t r = 0; r < count; ++r) {
                std::fprintf(md_file, "| %zu | `%s` | `%.3f ms` |\n", r + 1, read_spans[r].name,
                             read_spans[r].duration_nano / 1000000.0);
            }
            std::fprintf(md_file, "\n");
        }

        if (!active_spans.empty()) {
            std::fprintf(md_file, "## ⚙️ Top Active-Processing Spans\n\n");
            std::fprintf(md_file, "| Rank | Span Name | Duration (ms) |\n");
            std::fprintf(md_file, "| :--- | :--- | :--- |\n");
            std::qsort(active_spans.data(), active_spans.size(), sizeof(SpanStat),
                       compare_span_stats);
            std::size_t count = std::min(active_spans.size(), static_cast<std::size_t>(5));
            for (std::size_t r = 0; r < count; ++r) {
                std::fprintf(md_file, "| %zu | `%s` | `%.3f ms` |\n", r + 1, active_spans[r].name,
                             active_spans[r].duration_nano / 1000000.0);
            }
            std::fprintf(md_file, "\n");
        }

        if (!write_spans.empty()) {
            std::fprintf(md_file, "## 📤 Top gRPC Write Spans\n\n");
            std::fprintf(md_file, "| Rank | Span Name | Duration (ms) |\n");
            std::fprintf(md_file, "| :--- | :--- | :--- |\n");
            std::qsort(write_spans.data(), write_spans.size(), sizeof(SpanStat),
                       compare_span_stats);
            std::size_t count = std::min(write_spans.size(), static_cast<std::size_t>(5));
            for (std::size_t r = 0; r < count; ++r) {
                std::fprintf(md_file, "| %zu | `%s` | `%.3f ms` |\n", r + 1, write_spans[r].name,
                             write_spans[r].duration_nano / 1000000.0);
            }
            std::fprintf(md_file, "\n");
        }

        if (!cache_spans.empty()) {
            std::fprintf(md_file, "## 💾 Top Cache & Runtime Queue Spans\n\n");
            std::fprintf(md_file, "| Rank | Span Name | Duration (ms) |\n");
            std::fprintf(md_file, "| :--- | :--- | :--- |\n");
            std::qsort(cache_spans.data(), cache_spans.size(), sizeof(SpanStat),
                       compare_span_stats);
            std::size_t count = std::min(cache_spans.size(), static_cast<std::size_t>(5));
            for (std::size_t r = 0; r < count; ++r) {
                std::fprintf(md_file, "| %zu | `%s` | `%.3f ms` |\n", r + 1, cache_spans[r].name,
                             cache_spans[r].duration_nano / 1000000.0);
            }
            std::fprintf(md_file, "\n");
        }

        std::fprintf(md_file, "> [!NOTE]\n");
        std::fprintf(md_file, "> Dominant latency class breakdown allows diagnosing performance "
                              "regressions under heavy concurrency.\n");
        std::fclose(md_file);
    }

    struct CoalescingGroupStats {
        char group_id[64]{};
        std::uint64_t lifecycle_generation = 0;
        char leader_request_id[32]{};
        std::uint64_t leader_publish_result_unix_nano = 0;
        std::size_t client_requests = 0;
        std::size_t leaders = 0;
        std::size_t followers = 0;
        std::size_t followers_attached = 0;
        std::size_t followers_served_from_result = 0;
        std::size_t followers_timeout = 0;
        std::size_t followers_timed_out_before_publish = 0;
        std::size_t followers_timed_out_after_publish = 0;
        std::size_t fallbacks = 0;
        std::size_t upstream_calls = 0;
        std::size_t upstream_unknown = 0;
        char failure_reason[64]{};
    };

    std::vector<CoalescingGroupStats> coalescing_groups;
    for (std::uint32_t i = start_idx; i < end_idx; ++i) {
        std::size_t ring_idx = i % max_rec;
        const auto& record = ring->records[ring_idx];
        if (record.span_count > 0 && record.coalescing_has_context &&
            record.coalescing_group_id[0] != '\0') {
            CoalescingGroupStats* grp = nullptr;
            for (auto& g : coalescing_groups) {
                if (std::strcmp(g.group_id, record.coalescing_group_id) == 0) {
                    grp = &g;
                    break;
                }
            }
            if (grp == nullptr) {
                CoalescingGroupStats new_g{};
                std::strncpy(new_g.group_id, record.coalescing_group_id,
                             sizeof(new_g.group_id) - 1);
                coalescing_groups.push_back(new_g);
                grp = &coalescing_groups.back();
            }

            grp->client_requests++;
            if (record.coalescing_lifecycle_generation > 0) {
                grp->lifecycle_generation = record.coalescing_lifecycle_generation;
            }
            if (record.coalescing_leader_request_id[0] != '\0' &&
                grp->leader_request_id[0] == '\0') {
                std::strncpy(grp->leader_request_id, record.coalescing_leader_request_id,
                             sizeof(grp->leader_request_id) - 1);
                grp->leader_request_id[sizeof(grp->leader_request_id) - 1] = '\0';
            }
            if (std::strcmp(record.coalescing_role, "leader") == 0) {
                grp->leaders++;
                for (std::size_t j = 0; j < record.span_count; ++j) {
                    const auto& span = record.spans[j];
                    if (std::strcmp(span.name, kSpanCoalescingLeaderPublishResult) == 0) {
                        grp->leader_publish_result_unix_nano = span.start_unix_nano;
                        break;
                    }
                }
            } else if (std::strcmp(record.coalescing_role, "follower") == 0) {
                grp->followers++;
            }

            if (std::strcmp(record.coalescing_attach_result, "success") == 0) {
                grp->followers_attached++;
            }

            if (std::strcmp(record.coalescing_decision, "follower_consume_result") == 0) {
                grp->followers_served_from_result++;
            } else if (std::strcmp(record.coalescing_decision, "follower_timeout_fallback") == 0) {
                grp->fallbacks++;
            }

            if (std::strcmp(record.coalescing_wakeup_reason, "timeout") == 0) {
                grp->followers_timeout++;
            }

            if (record.coalescing_upstream_call_reason[0] != '\0' &&
                std::strcmp(record.coalescing_upstream_call_reason, "none") != 0) {
                grp->upstream_calls++;
                if (std::strcmp(record.coalescing_upstream_call_reason, "unknown") == 0) {
                    grp->upstream_unknown++;
                }
            }
        }
    }

    for (std::uint32_t i = start_idx; i < end_idx; ++i) {
        std::size_t ring_idx = i % max_rec;
        const auto& record = ring->records[ring_idx];
        if (!(record.span_count > 0 && record.coalescing_has_context &&
              record.coalescing_group_id[0] != '\0' &&
              std::strcmp(record.coalescing_wakeup_reason, "timeout") == 0)) {
            continue;
        }

        CoalescingGroupStats* grp = nullptr;
        for (auto& g : coalescing_groups) {
            if (std::strcmp(g.group_id, record.coalescing_group_id) == 0) {
                grp = &g;
                break;
            }
        }
        if (grp == nullptr) {
            continue;
        }

        std::uint64_t follower_wait_end_unix_nano = 0;
        for (std::size_t j = 0; j < record.span_count; ++j) {
            const auto& span = record.spans[j];
            if (std::strcmp(span.name, kSpanCoalescingFollowerWait) == 0) {
                follower_wait_end_unix_nano = span.end_unix_nano;
                break;
            }
        }

        if (grp->leader_publish_result_unix_nano == 0 ||
            (follower_wait_end_unix_nano > 0 &&
             follower_wait_end_unix_nano < grp->leader_publish_result_unix_nano)) {
            grp->followers_timed_out_before_publish++;
        } else {
            grp->followers_timed_out_after_publish++;
        }
    }

    if (!coalescing_groups.empty()) {
        char summary_md_path[512];
        std::snprintf(summary_md_path, sizeof(summary_md_path), "%s/%s_%s.trace_summary.md",
                      out_dir.c_str(), scen, time_str);
        std::FILE* s_file = std::fopen(summary_md_path, "w");
        if (s_file != nullptr) {
            std::fprintf(s_file, "# 🧪 ByteTaper Coalescing Group-level Trace Summary\n\n");
            std::fprintf(s_file,
                         "| Group ID | Lifecycle Generation | Leader Request ID | Client "
                         "Requests | Leaders | Followers | Followers Attached | Served from "
                         "Result | Followers Timeout | Timeouts Before Publish | Timeouts After "
                         "Publish | Fallbacks | Upstream Calls | Unknown Upstreams | Failure "
                         "Reason | Verdict |\n");
            std::fprintf(
                s_file,
                "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | "
                ":--- | :--- | :--- | :--- | :--- |\n");

            for (auto& g : coalescing_groups) {
                if (g.upstream_calls > g.leaders) {
                    std::strncpy(g.failure_reason, "upstream_call_leak",
                                 sizeof(g.failure_reason) - 1);
                } else if (g.followers_timed_out_before_publish > 0 &&
                           g.followers_timed_out_before_publish >=
                               g.followers_timed_out_after_publish) {
                    std::strncpy(g.failure_reason, "followers_timeout_before_result_publication",
                                 sizeof(g.failure_reason) - 1);
                } else if (g.followers_timed_out_after_publish > 0) {
                    std::strncpy(g.failure_reason, "followers_timeout_after_result_publication",
                                 sizeof(g.failure_reason) - 1);
                } else {
                    std::strncpy(g.failure_reason, "none", sizeof(g.failure_reason) - 1);
                }

                bool pass = (g.upstream_calls <= g.leaders) && (g.fallbacks == 0);
                std::fprintf(s_file,
                             "| `%s` | `%llu` | `%s` | `%zu` | `%zu` | `%zu` | `%zu` | `%zu` | "
                             "`%zu` | `%zu` | `%zu` | `%zu` | `%zu` | `%zu` | `%s` | **%s** |\n",
                             g.group_id, static_cast<unsigned long long>(g.lifecycle_generation),
                             g.leader_request_id[0] != '\0' ? g.leader_request_id : "0",
                             g.client_requests, g.leaders, g.followers, g.followers_attached,
                             g.followers_served_from_result, g.followers_timeout,
                             g.followers_timed_out_before_publish,
                             g.followers_timed_out_after_publish, g.fallbacks, g.upstream_calls,
                             g.upstream_unknown, g.failure_reason, pass ? "PASS" : "FAIL");
            }
            std::fprintf(s_file, "\n");
            std::fprintf(s_file, "## 📝 Detailed Analysis\n\n");
            for (const auto& g : coalescing_groups) {
                bool pass = (g.upstream_calls <= g.leaders) && (g.fallbacks == 0);
                std::fprintf(s_file, "### Group `%s`\n", g.group_id);
                if (pass) {
                    std::fprintf(s_file, "- **Verdict**: PASS\n");
                    std::fprintf(
                        s_file,
                        "- **Suppression efficiency**: All client requests were satisfied with at "
                        "most 1 upstream call per leader and zero fallback timeout failures.\n");
                } else {
                    std::fprintf(s_file, "- **Verdict**: FAIL\n");
                    std::fprintf(s_file, "- **Diagnostics**:\n");
                    if (g.fallbacks > 0) {
                        std::fprintf(s_file,
                                     "  - Detected `%zu` follower fallback timeouts. Followers "
                                     "timed out before the leader could publish a result.\n",
                                     g.fallbacks);
                    }
                    if (g.upstream_calls > g.leaders) {
                        std::fprintf(s_file,
                                     "  - Detected extra upstream calls: `%zu` upstream calls for "
                                     "`%zu` leaders.\n",
                                     g.upstream_calls, g.leaders);
                    }
                    if (g.upstream_unknown > 0) {
                        std::fprintf(s_file,
                                     "  - Detected `%zu` unexplained or untracked upstream "
                                     "dispatches (upstream_call_reason == unknown).\n",
                                     g.upstream_unknown);
                    }
                    if (g.failure_reason[0] != '\0' && std::strcmp(g.failure_reason, "none") != 0) {
                        std::fprintf(s_file, "  - Failure reason: `%s`.\n", g.failure_reason);
                    }
                }
                std::fprintf(s_file, "\n");
            }
            std::fclose(s_file);
        }
    }
}

} // namespace bytetaper::observability

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_OBSERVABILITY_TRACE_H
#define BYTETAPER_OBSERVABILITY_TRACE_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bytetaper::observability {

struct TraceId {
    char value[32] = { 0 };
};

struct SpanId {
    char value[16] = { 0 };
};

enum class TraceSpanKind {
    Internal,
    Server,
    Client,
    Producer,
    Consumer,
};

enum class TraceLatencyClass {
    Unknown,
    GrpcReadWait,
    ActiveProcessing,
    ActiveProcessingDetail, // child spans under an active parent; excluded from totals
    GrpcWrite,
    RuntimeQueueWait,
    CacheIo,
    UpstreamWait,
    Mixed,
};

struct TraceSpan {
    SpanId span_id{};
    SpanId parent_span_id{};
    char name[96]{};

    TraceSpanKind kind = TraceSpanKind::Internal;

    std::uint64_t start_unix_nano = 0;
    std::uint64_t end_unix_nano = 0;
    std::uint64_t duration_nano = 0;

    TraceLatencyClass latency_class = TraceLatencyClass::Unknown;
};

static constexpr std::size_t kMaxSpansPerTrace = 32;

struct TraceRecord {
    TraceId trace_id{};
    SpanId root_span_id{};

    char scenario[64]{};
    char route_id[64]{};
    char request_path[256]{};

    std::uint16_t response_status_code = 0;
    std::size_t response_body_len = 0;
    bool response_body_size_known = false;

    bool compression_candidate = false;
    bool compression_decision_final = false;
    bool cache_hit = false;
    bool coalescing_leader = false;
    bool coalescing_follower = false;

    std::uint64_t total_duration_nano = 0;
    std::uint64_t total_read_wait_nano = 0;
    std::uint64_t total_active_processing_nano = 0;
    std::uint64_t total_grpc_write_nano = 0;
    std::uint64_t total_runtime_queue_wait_nano = 0;
    std::uint64_t total_cache_io_nano = 0;

    TraceLatencyClass dominant_latency_class = TraceLatencyClass::Unknown;

    TraceSpan spans[kMaxSpansPerTrace];
    std::size_t span_count = 0;
};

struct TraceConfig {
    bool enabled = false;
    char mode[16] = "slow"; // off | slow | sampled | all
    char output_dir[256] = "reports/traces";
    std::uint64_t slow_total_ms = 30;
    std::uint64_t slow_phase_ms = 10;
    std::uint32_t sample_rate_ppm = 10000; // parts-per-million (1% = 10000)
    std::uint32_t max_records = 4096;
    std::uint32_t max_per_route = 1024;
};

struct RouteTraceQuota {
    char route_id[64] = { 0 };
    std::atomic<std::uint32_t> count{ 0 };
};

static constexpr std::size_t kMaxTracedRoutes = 64;

struct TraceRingBuffer {
    TraceRecord records[4096]; // max_records upper bound
    std::atomic<std::uint32_t> write_index{ 0 };
    std::atomic<std::uint64_t> dropped_count{ 0 };
    RouteTraceQuota route_quotas[kMaxTracedRoutes];
};

// Scoped span: no destructor — caller must call end() explicitly
struct TraceSpanScope {
    TraceSpan* span = nullptr;
    void end(); // sets end_unix_nano, computes duration_nano
};

// Config
TraceConfig trace_config_from_env();
void trace_init(const TraceConfig& config);
const TraceConfig& trace_global_config();
TraceRingBuffer* trace_global_ring();

// Per-request lifecycle
void trace_start_record(TraceRecord* record, const char* scenario);
void trace_set_route(TraceRecord* record, const char* route_id, const char* request_path);
void trace_set_compression_info(TraceRecord* record, bool candidate, bool decision_final,
                                std::size_t body_len, bool body_size_known);
void trace_set_cache_info(TraceRecord* record, bool cache_hit);
void trace_set_coalescing_info(TraceRecord* record, bool leader, bool follower);
const char* trace_latency_class_str(TraceLatencyClass cls);
void trace_classify(TraceRecord* record); // derives totals + dominant_latency_class
bool trace_is_slow(const TraceRecord& record, const TraceConfig& config);
bool trace_is_sampled(const TraceConfig& config);

// Span API
TraceSpanScope trace_start_span(TraceRecord* record, const char* name,
                                TraceLatencyClass latency_class, const SpanId* parent_span_id);
void trace_rename_span(TraceSpan* span, const char* name); // for read-wait rename after classify

// Ring buffer
bool trace_push(TraceRingBuffer* ring, const TraceConfig& config, const TraceRecord& record);

// Flush
void trace_flush(TraceRingBuffer* ring, const TraceConfig& config, const char* scenario);

// Format one record to JSON line (buf must be >= 8192 bytes)
std::size_t trace_format_jsonl(const TraceRecord& record, char* buf, std::size_t buf_size);

} // namespace bytetaper::observability

#endif // BYTETAPER_OBSERVABILITY_TRACE_H

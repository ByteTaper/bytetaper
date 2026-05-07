// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "observability/trace.h"

#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace bytetaper::observability {

TEST(TraceTest, ConfigLoadingFromEnv) {
    ::setenv("BYTETAPER_TRACE_ENABLED", "true", 1);
    ::setenv("BYTETAPER_TRACE_MODE", "sampled", 1);
    ::setenv("BYTETAPER_TRACE_OUTPUT_DIR", "custom/traces", 1);
    ::setenv("BYTETAPER_TRACE_SLOW_TOTAL_MS", "50", 1);
    ::setenv("BYTETAPER_TRACE_SLOW_PHASE_MS", "8", 1);
    ::setenv("BYTETAPER_TRACE_SAMPLE_RATE", "0.01", 1);
    ::setenv("BYTETAPER_TRACE_MAX_RECORDS", "128", 1);

    TraceConfig config = trace_config_from_env();
    EXPECT_TRUE(config.enabled);
    EXPECT_STREQ(config.mode, "sampled");
    EXPECT_STREQ(config.output_dir, "custom/traces");
    EXPECT_EQ(config.slow_total_ms, 50);
    EXPECT_EQ(config.slow_phase_ms, 8);
    EXPECT_EQ(config.sample_rate_ppm, 10000);
    EXPECT_EQ(config.max_records, 128);

    ::unsetenv("BYTETAPER_TRACE_ENABLED");
    ::unsetenv("BYTETAPER_TRACE_MODE");
    ::unsetenv("BYTETAPER_TRACE_OUTPUT_DIR");
    ::unsetenv("BYTETAPER_TRACE_SLOW_TOTAL_MS");
    ::unsetenv("BYTETAPER_TRACE_SLOW_PHASE_MS");
    ::unsetenv("BYTETAPER_TRACE_SAMPLE_RATE");
    ::unsetenv("BYTETAPER_TRACE_MAX_RECORDS");
}

TEST(TraceTest, TraceIdContainsMultipleSpans) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    EXPECT_NE(record.trace_id.value[0], '\0');
    EXPECT_NE(record.root_span_id.value[0], '\0');

    auto s1 = trace_start_span(&record, "span1", TraceLatencyClass::ActiveProcessing,
                               &record.root_span_id);
    auto s2 = trace_start_span(&record, "span2", TraceLatencyClass::ActiveProcessing,
                               &record.root_span_id);

    ASSERT_NE(s1.span, nullptr);
    ASSERT_NE(s2.span, nullptr);
    EXPECT_STRNE(s1.span->span_id.value, s2.span->span_id.value);
    EXPECT_STREQ(s1.span->parent_span_id.value, record.root_span_id.value);
    EXPECT_STREQ(s2.span->parent_span_id.value, record.root_span_id.value);
}

TEST(TraceTest, ReadWaitClassifiedCorrectly) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto r1 =
        trace_start_span(&record, "read1", TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    r1.end();
    r1.span->duration_nano = 50 * 1000000ULL; // 50ms

    trace_classify(&record);
    EXPECT_EQ(record.total_read_wait_nano, 50 * 1000000ULL);
}

TEST(TraceTest, ActiveSpanExcludesWriteTime) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto active = trace_start_span(&record, "active", TraceLatencyClass::ActiveProcessing,
                                   &record.root_span_id);
    active.end();
    active.span->duration_nano = 20 * 1000000ULL;

    auto write =
        trace_start_span(&record, "write", TraceLatencyClass::GrpcWrite, &record.root_span_id);
    write.end();
    write.span->duration_nano = 30 * 1000000ULL;

    trace_classify(&record);
    EXPECT_EQ(record.total_active_processing_nano, 20 * 1000000ULL);
    EXPECT_EQ(record.total_grpc_write_nano, 30 * 1000000ULL);
}

TEST(TraceTest, DominantClassGrpcReadWait) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    record.total_duration_nano = 100 * 1000000ULL;

    auto s =
        trace_start_span(&record, "wait", TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    s.end();
    s.span->duration_nano = 80 * 1000000ULL;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::GrpcReadWait);
}

TEST(TraceTest, DominantClassGrpcWrite) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    record.total_duration_nano = 100 * 1000000ULL;

    auto s = trace_start_span(&record, "write", TraceLatencyClass::GrpcWrite, &record.root_span_id);
    s.end();
    s.span->duration_nano = 70 * 1000000ULL;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::GrpcWrite);
}

TEST(TraceTest, DominantClassActiveProcessing) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    record.total_duration_nano = 100 * 1000000ULL;

    auto s = trace_start_span(&record, "active", TraceLatencyClass::ActiveProcessing,
                              &record.root_span_id);
    s.end();
    s.span->duration_nano = 65 * 1000000ULL;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::ActiveProcessing);
}

TEST(TraceTest, DominantClassMixed) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    record.total_duration_nano = 100 * 1000000ULL;

    auto s1 =
        trace_start_span(&record, "wait", TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    s1.end();
    s1.span->duration_nano = 30 * 1000000ULL;

    auto s2 = trace_start_span(&record, "active", TraceLatencyClass::ActiveProcessing,
                               &record.root_span_id);
    s2.end();
    s2.span->duration_nano = 30 * 1000000ULL;

    auto s3 =
        trace_start_span(&record, "write", TraceLatencyClass::GrpcWrite, &record.root_span_id);
    s3.end();
    s3.span->duration_nano = 30 * 1000000ULL;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::Mixed);
}

TEST(TraceTest, PerRouteQuotaPreventsCrossRouteEvict) {
    TraceConfig config{};
    config.enabled = true;
    config.max_records = 10;
    config.max_per_route = 2;

    auto ring_ptr = std::make_unique<TraceRingBuffer>();
    TraceRingBuffer& ring = *ring_ptr;
    for (std::size_t i = 0; i < kMaxTracedRoutes; ++i) {
        ring.route_quotas[i].route_id[0] = '\0';
        ring.route_quotas[i].count.store(0, std::memory_order_relaxed);
    }

    TraceRecord recordA{};
    trace_start_record(&recordA, "scen");
    trace_set_route(&recordA, "routeA", "/path/a");

    // Push route A up to max_per_route limit
    EXPECT_TRUE(trace_push(&ring, config, recordA));
    EXPECT_TRUE(trace_push(&ring, config, recordA));

    // Third push for route A should drop
    EXPECT_FALSE(trace_push(&ring, config, recordA));
    EXPECT_EQ(ring.dropped_count.load(), 1);

    // Route B should still be allowed to push because its quota is untouched
    TraceRecord recordB{};
    trace_start_record(&recordB, "scen");
    trace_set_route(&recordB, "routeB", "/path/b");

    EXPECT_TRUE(trace_push(&ring, config, recordB));
}

TEST(TraceTest, RingBufferDropsWhenFull) {
    TraceConfig config{};
    config.enabled = true;
    config.max_records = 4;
    config.max_per_route = 10;

    auto ring_ptr = std::make_unique<TraceRingBuffer>();
    TraceRingBuffer& ring = *ring_ptr;
    for (std::size_t i = 0; i < kMaxTracedRoutes; ++i) {
        ring.route_quotas[i].route_id[0] = '\0';
        ring.route_quotas[i].count.store(0, std::memory_order_relaxed);
    }

    TraceRecord record{};
    trace_start_record(&record, "scen");
    trace_set_route(&record, "routeA", "/path/a");

    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(trace_push(&ring, config, record));
    }
    EXPECT_EQ(ring.dropped_count.load(), 0);

    // This should overwrite, drop count goes up, no deadlocks
    EXPECT_TRUE(trace_push(&ring, config, record));
    EXPECT_EQ(ring.dropped_count.load(), 1);
}

TEST(TraceTest, SampledModeRejectsNonSampled) {
    TraceConfig config{};
    config.enabled = true;
    config.sample_rate_ppm = 10000; // 1%

    int accepted = 0;
    for (int i = 0; i < 1000; ++i) {
        if (trace_is_sampled(config)) {
            accepted++;
        }
    }
    // With 1% rate and 1000 tries, we expect around 10 +/- standard deviation
    EXPECT_GE(accepted, 1);
    EXPECT_LE(accepted, 50);
}

TEST(TraceTest, JsonlOutputContainsDominantClass) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    trace_set_route(&record, "routeA", "/path/a");
    record.total_duration_nano = 100 * 1000000ULL;

    auto s =
        trace_start_span(&record, "wait", TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    s.end();
    s.span->duration_nano = 80 * 1000000ULL;

    trace_classify(&record);

    char buf[8192];
    std::size_t size = trace_format_jsonl(record, buf, sizeof(buf));
    ASSERT_GT(size, 0);

    EXPECT_NE(std::strstr(buf, "\"dominant_latency_class\":\"grpc_read_wait\""), nullptr);
}

TEST(TraceTest, BenchmarkLegAbsentFromJsonl) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    trace_set_route(&record, "routeA", "/path/a");

    char buf[8192];
    std::size_t size = trace_format_jsonl(record, buf, sizeof(buf));
    ASSERT_GT(size, 0);

    EXPECT_EQ(std::strstr(buf, "\"benchmark_leg\""), nullptr);
}

TEST(TraceTest, ActiveProcessingTotalExcludesRootSpan) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    // Root span has TraceLatencyClass::Unknown (which is TraceSpanKind::Internal kind, or Unknown
    // class)
    EXPECT_EQ(record.spans[0].latency_class, TraceLatencyClass::Unknown);
    record.spans[0].duration_nano = 1000;

    trace_classify(&record);
    EXPECT_EQ(record.total_active_processing_nano, 0);
}

TEST(TraceTest, ActiveProcessingTotalExcludesReadWait) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto s =
        trace_start_span(&record, "read", TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    s.end();
    s.span->duration_nano = 1000;

    trace_classify(&record);
    EXPECT_EQ(record.total_active_processing_nano, 0);
    EXPECT_EQ(record.total_read_wait_nano, 1000);
}

TEST(TraceTest, ActiveProcessingTotalExcludesWriteSpan) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto s = trace_start_span(&record, "write", TraceLatencyClass::GrpcWrite, &record.root_span_id);
    s.end();
    s.span->duration_nano = 1000;

    trace_classify(&record);
    EXPECT_EQ(record.total_active_processing_nano, 0);
    EXPECT_EQ(record.total_grpc_write_nano, 1000);
}

TEST(TraceTest, ActiveProcessingDetailExcludedFromTotal) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto child = trace_start_span(&record, "detail", TraceLatencyClass::ActiveProcessingDetail,
                                  &record.root_span_id);
    child.end();
    child.span->duration_nano = 500;

    trace_classify(&record);
    EXPECT_EQ(record.total_active_processing_nano, 0);
}

TEST(TraceTest, DominantClassRuntimeQueueWait) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    record.total_duration_nano = 1000;

    auto s = trace_start_span(&record, "queue", TraceLatencyClass::RuntimeQueueWait,
                              &record.root_span_id);
    s.end();
    s.span->duration_nano = 650;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::RuntimeQueueWait);
}

TEST(TraceTest, DominantClassCacheIo) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    record.total_duration_nano = 1000;

    auto s = trace_start_span(&record, "cache", TraceLatencyClass::CacheIo, &record.root_span_id);
    s.end();
    s.span->duration_nano = 650;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::CacheIo);
}

TEST(TraceTest, DominantClassUnknownWhenZeroDuration) {
    TraceRecord record{};
    trace_start_record(&record, "scen");
    record.total_duration_nano = 0;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::Unknown);
}

TEST(TraceTest, GenericReadWaitNotEmitted) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto r = trace_start_span(&record, "bytetaper.grpc.read.wait.headers",
                              TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    r.end();

    char buf[8192];
    std::size_t size = trace_format_jsonl(record, buf, sizeof(buf));
    ASSERT_GT(size, 0);

    // No exact match for "bytetaper.grpc.read.wait" as an entire string name
    EXPECT_EQ(std::strstr(buf, "\"name\":\"bytetaper.grpc.read.wait\""), nullptr);
}

TEST(TraceTest, StreamCloseSpanNamedCorrectly) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto r = trace_start_span(&record, "bytetaper.grpc.read.wait", TraceLatencyClass::GrpcReadWait,
                              &record.root_span_id);
    r.end();
    trace_rename_span(r.span, "bytetaper.grpc.read.wait.stream_close");

    char buf[8192];
    std::size_t size = trace_format_jsonl(record, buf, sizeof(buf));
    ASSERT_GT(size, 0);

    EXPECT_NE(std::strstr(buf, "\"name\":\"bytetaper.grpc.read.wait.stream_close\""), nullptr);
}

TEST(TraceTest, ActiveEndsBeforeWriteBegins) {
    TraceRecord record{};
    trace_start_record(&record, "scen");

    auto active = trace_start_span(&record, "bytetaper.active", TraceLatencyClass::ActiveProcessing,
                                   &record.root_span_id);
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    active.end();

    auto write = trace_start_span(&record, "bytetaper.write", TraceLatencyClass::GrpcWrite,
                                  &record.root_span_id);
    std::this_thread::sleep_for(std::chrono::microseconds(10));
    write.end();

    EXPECT_LE(active.span->end_unix_nano, write.span->start_unix_nano);
}

// Integration verification mocks & tests
TEST(TraceTest, OneTraceHasCorrectPhaseBreakdown) {
    TraceRecord record{};
    trace_start_record(&record, "integration");
    record.total_duration_nano = 2000;

    auto read =
        trace_start_span(&record, "read", TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    read.end();
    read.span->duration_nano = 500;

    auto active = trace_start_span(&record, "active", TraceLatencyClass::ActiveProcessing,
                                   &record.root_span_id);
    active.end();
    active.span->duration_nano = 800;

    auto write =
        trace_start_span(&record, "write", TraceLatencyClass::GrpcWrite, &record.root_span_id);
    write.end();
    write.span->duration_nano = 300;

    trace_classify(&record);
    EXPECT_EQ(record.total_read_wait_nano, 500);
    EXPECT_EQ(record.total_active_processing_nano, 800);
    EXPECT_EQ(record.total_grpc_write_nano, 300);
    EXPECT_LT(record.total_active_processing_nano, record.total_duration_nano);
}

TEST(TraceTest, ReadWaitDominatesWhenStreamWaits) {
    TraceRecord record{};
    trace_start_record(&record, "integration");
    record.total_duration_nano = 1000;

    auto read =
        trace_start_span(&record, "read", TraceLatencyClass::GrpcReadWait, &record.root_span_id);
    read.end();
    read.span->duration_nano = 700;

    trace_classify(&record);
    EXPECT_EQ(record.dominant_latency_class, TraceLatencyClass::GrpcReadWait);
}

} // namespace bytetaper::observability

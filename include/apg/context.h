// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_APG_CONTEXT_H
#define BYTETAPER_APG_CONTEXT_H

#include "apg/query_view.h"
#include "cache/cache_entry.h"
#include "cache/l1_cache.h"
#include "cache/l2_disk_cache.h"
#include "coalescing/coalescing_decision.h"
#include "compression/compression_decision.h"
#include "metrics/cache_metrics.h"
#include "metrics/coalescing_metrics.h"
#include "metrics/compression_metrics.h"
#include "metrics/pagination_metrics.h"
#include "metrics/runtime_metrics.h"
#include "policy/field_filter_policy.h"
#include "policy/route_policy.h"
#include "runtime/worker_queue.h"

#include <cstddef>
#include <cstdint>

namespace bytetaper::coalescing {
struct FollowerWaitPool;
} // namespace bytetaper::coalescing

namespace bytetaper::runtime {
struct WorkerQueue;
} // namespace bytetaper::runtime

namespace bytetaper::observability {
struct TraceRecord;
}

namespace bytetaper::apg {

struct RequestMutationOutput {
    char path[2048] = {}; // full mutated path+query (e.g., "/orders?limit=50")
    std::size_t path_length = 0;
    bool applied = false;
    const char* reason = nullptr;     // static string: "missing_limit", "limit_exceeds_max"
    std::uint32_t limit_to_apply = 0; // value written to path; used for header
};

struct CompressionDecisionOutput {
    bool evaluated = false;
    bool candidate = false;
    compression::CompressionSkipReason skip_reason = compression::CompressionSkipReason::None;
    policy::CompressionAlgorithm algorithm_hint = policy::CompressionAlgorithm::None;
};

struct ApgTransformContext {
    static constexpr std::size_t kRawPathBufferSize = 1024;
    static constexpr std::size_t kRawQueryBufferSize = 1024;

    std::uint64_t request_id = 0;
    std::size_t input_payload_bytes = 0;
    std::size_t output_payload_bytes = 0;
    std::uint32_t executed_stage_count = 0;
    char raw_path[kRawPathBufferSize] = {};
    std::size_t raw_path_length = 0;
    char raw_query[kRawQueryBufferSize] = {};
    std::size_t raw_query_length = 0;
    RequestQueryView request_query_view = {};
    bool request_query_view_ready = false;
    char selected_fields[policy::kMaxFields][policy::kMaxFieldNameLen] = {};
    // Canonical API-intelligence metric.
    // This count represents selected fields after policy filtering.
    std::size_t selected_field_count = 0;
    // Canonical API-intelligence metric for JSON transform filtering.
    // This count represents object field keys removed by filtering in the
    // latest transform call.
    mutable std::size_t removed_field_count = 0;
    char* trace_buffer = nullptr;
    std::size_t trace_capacity = 0;
    std::size_t trace_length = 0;

    // --- Cache lookup inputs (set by caller before running pipeline) ---
    const policy::RoutePolicy* matched_policy = nullptr;
    cache::L1Cache* l1_cache = nullptr;
    policy::HttpMethod request_method = policy::HttpMethod::Get;
    std::int64_t request_epoch_ms = 0;

    // --- Prepared cache key (written once by cache_key_prepare_stage) ---
    char cache_key[cache::kCacheKeyMaxLen] = {};
    bool cache_key_ready = false; // true iff build_cache_key succeeded
    bool cache_eligible = false;  // true iff method==GET and policy.cache==Store

    // --- Cache lookup outputs (written by l1_cache_lookup_stage) ---
    bool cache_hit = false;
    const char* cache_layer = nullptr; // "L1" on hit, nullptr on miss
    bool should_return_immediate_response = false;
    cache::CacheEntry cached_response{}; // populated on hit; body is non-owning

    // --- Cache store inputs (set by caller after receiving upstream response) ---
    std::uint16_t response_status_code = 0;
    const char* response_body = nullptr; // non-owning
    std::size_t response_body_len = 0;
    bool response_body_size_known = false;
    char response_content_type[cache::kCacheContentTypeMaxLen] = {};
    std::size_t response_content_type_len = 0;

    static constexpr std::size_t kL2BodyBufSize = 65536; // 64 KiB

    // --- L2 cache input (set by caller before running pipeline) ---
    cache::L2DiskCache* l2_cache = nullptr;

    // --- L2 body buffer (owned by context; l2_cache_lookup_stage writes body here) ---
    char l2_body_buf[kL2BodyBufSize] = {};

    RequestMutationOutput request_mutation = {};

    // --- Pagination oversized-response warning (written by response-body phase caller) ---
    bool pagination_warning = false;
    const char* pagination_warning_reason = nullptr; // static string: "response_still_oversized"

    // --- Compression inputs (populated across request/response phases) ---
    compression::AcceptEncoding client_accept_encoding{};
    compression::ContentEncodingResult response_content_encoding{};
    // --- Compression decision output (written by compression_decision_stage) ---
    CompressionDecisionOutput compression_decision{};
    bool compression_decision_final = false;

    // --- Coalescing decision output (written by coalescing_decision_stage) ---
    coalescing::CoalescingDecision coalescing_decision{};
    coalescing::InFlightRegistry* coalescing_registry = nullptr;
    coalescing::FollowerWaitPool* follower_wait_pool = nullptr;

    // --- Coalescing extra state (BT-037F) ---
    metrics::UpstreamCallReason coalescing_upstream_reason = metrics::UpstreamCallReason::Unknown;
    coalescing::AttachFailureReason coalescing_attach_failure_reason =
        coalescing::AttachFailureReason::None;
    const char* coalescing_result_source = nullptr; // "NONE", "L1", "COALESCED"
    std::uint32_t coalescing_group_id = 0;
    std::uint64_t coalescing_key_hash = 0;
    coalescing::CoalescingState coalescing_terminal_state =
        coalescing::CoalescingState::LeaderRunning;
    bool coalescing_terminal_result_join_flag = false;

    // --- Coalescing extra string state (BT-037G) ---
    bool coalescing_has_context = false;
    char coalescing_role_str[32]{};
    char coalescing_decision_str[64]{};
    char coalescing_attach_result_str[32]{};
    char coalescing_wakeup_reason_str[32]{};
    char coalescing_upstream_call_reason[64]{};
    char coalescing_group_id_str[64]{};
    char coalescing_lifecycle_generation_str[32]{};
    char coalescing_leader_request_id_str[32]{};

    // --- Trace context (non-owning pointer into StreamFilterState.trace) ---
    observability::TraceRecord* trace = nullptr;

    // --- Background runtime inputs (set by caller for async stages) ---
    runtime::WorkerQueue* worker_queue = nullptr;

    // --- Metrics (optional pointers to central registry counters) ---
    metrics::PaginationMetrics* pagination_metrics = nullptr;
    metrics::CacheMetrics* cache_metrics = nullptr;
    metrics::CompressionMetrics* compression_metrics = nullptr;
    metrics::CoalescingMetrics* coalescing_metrics = nullptr;
    metrics::RuntimeMetrics* runtime_metrics = nullptr;
};

} // namespace bytetaper::apg

#endif // BYTETAPER_APG_CONTEXT_H

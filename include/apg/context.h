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

namespace bytetaper::runtime {
struct WorkerQueue;
struct RouteCacheEpochStore;
} // namespace bytetaper::runtime

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

static constexpr std::size_t kMaxPendingInvalidationTargets = 8;
static constexpr std::size_t kInvalidationRouteIdMaxLen = 64;

enum class MutationInvalidationDecision : std::uint8_t {
    None,
    Prepared,
    Applied,
    SkippedNoPolicy,
    SkippedNonMutationMethod,
    SkippedMethodNotEnabled,
    SkippedStatusNotSuccessful,
    FailedEpochStoreMissing,
    FailedEpochBump,
};

struct PendingMutationInvalidationTarget {
    char route_id[kInvalidationRouteIdMaxLen] = {};
};

struct PendingMutationInvalidationPlan {
    bool prepared = false;
    bool applied = false;
    bool skipped = false;
    bool failed = false;
    MutationInvalidationDecision decision = MutationInvalidationDecision::None;
    std::uint16_t success_status_min = 200;
    std::uint16_t success_status_max = 299;
    PendingMutationInvalidationTarget targets[kMaxPendingInvalidationTargets] = {};
    std::size_t target_count = 0;
    const char* reason = nullptr;
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
    bool client_query_present = false;
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
    const policy::RoutePolicy* active_routes = nullptr;
    std::size_t active_route_count = 0;
    cache::L1Cache* l1_cache = nullptr;
    runtime::RouteCacheEpochStore* route_cache_epoch_store = nullptr;
    policy::HttpMethod request_method = policy::HttpMethod::Get;
    std::int64_t request_epoch_ms = 0;

    // --- Route cache epoch (populated by cache_key_prepare_stage) ---
    std::uint64_t route_cache_epoch = 0;
    bool route_cache_epoch_ready = false;

    // --- Prepared cache key (written once by cache_key_prepare_stage) ---
    char cache_key[cache::kCacheKeyMaxLen] = {};
    bool cache_key_ready = false; // true iff build_cache_key succeeded
    bool cache_eligible = false;  // true iff method==GET and policy.cache==Store

    // --- Request auth state (populated during request header handling) ---
    bool request_has_authorization = false;
    bool request_has_cookie = false;
    bool cache_auth_bypass = false;

    // --- Private cache scope (populated during request header handling) ---
    bool private_cache_scope_ready = false;
    char private_cache_scope_hash[32] = {};

    // --- Prepared header vary state (populated during request header handling) ---
    static constexpr std::size_t kMaxPreparedVaryHeaders = policy::kMaxCacheVaryHeaders;
    static constexpr std::size_t kPreparedVaryHashLen = 17; // 16 hex + null
    std::size_t cache_vary_count = 0;
    char cache_vary_names[kMaxPreparedVaryHeaders][policy::kMaxCacheVaryHeaderNameLen] = {};
    char cache_vary_value_hashes[kMaxPreparedVaryHeaders][kPreparedVaryHashLen] = {};
    bool cache_vary_ready = false;

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

    // Follower synchronous L2 read buffer. Responses larger than this are stored to L2
    // but cannot be delivered through the immediate follower path — see body-size contract
    // in include/coalescing/coalescing_completion_handoff.h.
    static constexpr std::size_t kL2BodyBufSize = 65536;

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

    // --- Background runtime inputs (set by caller for async stages) ---
    runtime::WorkerQueue* worker_queue = nullptr;

    // --- Materialized Field-Filtered Variant Cache ---
    char variant_cache_key[cache::kCacheKeyMaxLen] = {};
    bool variant_cache_key_ready = false;
    bool variant_admission_passed = false;
    char sanitized_query[kRawQueryBufferSize] = {};
    bool sanitized_query_ready = false;

    PendingMutationInvalidationPlan mutation_invalidation = {};

    // --- Metrics (optional pointers to central registry counters) ---
    metrics::PaginationMetrics* pagination_metrics = nullptr;
    metrics::CacheMetrics* cache_metrics = nullptr;
    metrics::CompressionMetrics* compression_metrics = nullptr;
    metrics::CoalescingMetrics* coalescing_metrics = nullptr;
    metrics::RuntimeMetrics* runtime_metrics = nullptr;
};

} // namespace bytetaper::apg

#endif // BYTETAPER_APG_CONTEXT_H

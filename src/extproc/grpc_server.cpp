// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/grpc_server.h"

#include "apg/pipeline.h"
#include "cache/cache_safety.h"
#include "compression/accept_encoding.h"
#include "compression/content_encoding.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "extproc/bytetaper_to_envoy.h"
#include "extproc/default_pipelines.h"
#include "extproc/header_view.h"
#include "extproc/reporting_headers.h"
#include "extproc/request_runtime.h"
#include "field_selection/request_target.h"
#include "json_transform/content_type.h"
#include "observability/trace.h"
#include "policy/route_matcher.h"
#include "runtime/worker_queue.h"
#include "safety/fail_open.h"
#include "stages/compression_decision_stage.h"
#include "stages/pagination_request_mutation_stage.h"

#include <chrono>
#include <cstddef>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace bytetaper::extproc {

namespace {

constexpr const char* kPathHeader = ":path";
constexpr const char* kContentTypeHeader = "content-type";
constexpr const char* kContentLengthHeader = "content-length";
constexpr const char* kOriginalBytesHeader = "x-bytetaper-original-bytes";
constexpr const char* kOptimizedBytesHeader = "x-bytetaper-optimized-bytes";
constexpr const char* kRoutePolicyHeader = "x-bytetaper-route-policy";

enum class ReportingHeaderMode {
    None,
    CompressionOnly,
    FullDiagnostics,
};

struct StreamFilterState {
    apg::ApgTransformContext context{};
    json_transform::JsonResponseKind response_kind =
        json_transform::JsonResponseKind::SkipUnsupported;
    bool has_query_selection = false;
    const policy::RoutePolicy* matched_policy = nullptr;
    bool is_non_2xx_response = false;

    // Lightweight compression state for compression-only routes
    stages::CompressionDecisionContext compression_context{};
    char owned_content_type[cache::kCacheContentTypeMaxLen] = {};
    bool compression_decision_final = false;

    observability::TraceRecord trace{};
};

void add_overwrite_header(envoy::service::ext_proc::v3::CommonResponse* common, const char* key,
                          const std::string& value) {
    if (common == nullptr || key == nullptr) {
        return;
    }
    auto* mutation = common->mutable_header_mutation()->add_set_headers();
    mutation->mutable_header()->set_key(key);
    mutation->mutable_header()->set_raw_value(value.data(), value.size());
    mutation->set_append_action(
        envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
}

void add_bytetaper_report_headers(envoy::service::ext_proc::v3::CommonResponse* common,
                                  std::size_t original_bytes, std::size_t optimized_bytes,
                                  std::size_t removed_fields, std::size_t saved_bytes, bool applied,
                                  bool cache_hit) {
    if (common == nullptr) {
        return;
    }
    add_overwrite_header(common, kResponseBodyHeader, kTrueValue);
    add_overwrite_header(common, kWasteRemovedFieldsHeader, std::to_string(removed_fields));
    add_overwrite_header(common, kWasteSavedBytesHeader, std::to_string(saved_bytes));
    add_overwrite_header(common, kOriginalResponseBytesHeader, std::to_string(original_bytes));
    add_overwrite_header(common, kOptimizedResponseBytesHeader, std::to_string(optimized_bytes));
    add_overwrite_header(common, kOriginalBytesHeader, std::to_string(original_bytes));
    add_overwrite_header(common, kOptimizedBytesHeader, std::to_string(optimized_bytes));
    add_overwrite_header(common, kTransformAppliedHeader, applied ? kTrueValue : kFalseValue);
    add_overwrite_header(common, kCachedResponseHeader, cache_hit ? kTrueValue : kFalseValue);
}

bool read_header_value(const envoy::config::core::v3::HeaderMap& headers, const char* key,
                       std::string* value_out) {
    if (key == nullptr || value_out == nullptr) {
        return false;
    }

    for (const auto& header : headers.headers()) {
        if (header.key() != key) {
            continue;
        }
        if (!header.raw_value().empty()) {
            *value_out = header.raw_value();
            return true;
        }
        *value_out = header.value();
        return true;
    }

    return false;
}

static void prepare_cache_auth_context(const envoy::config::core::v3::HeaderMap& headers,
                                       const extproc::RequestHeaderView& view,
                                       apg::ApgTransformContext* ctx) {
    ctx->request_has_authorization = (view.authorization != nullptr && view.authorization_len > 0);
    ctx->request_has_cookie = (view.cookie != nullptr && view.cookie_len > 0);

    const policy::RoutePolicy* pol = ctx->matched_policy;
    if (pol == nullptr) {
        return;
    }

    ctx->cache_auth_bypass = cache::cache_auth_bypass(
        ctx->request_has_authorization, ctx->request_has_cookie, pol->cache.private_cache);

    if (ctx->cache_auth_bypass || !pol->cache.private_cache) {
        return;
    }

    if (pol->cache.auth_scope_header[0] == '\0') {
        ctx->cache_auth_bypass = true;
        return;
    }

    const char* scope_val = nullptr;
    std::size_t scope_len = 0;
    if (!extproc::read_header_value_case_insensitive(headers, pol->cache.auth_scope_header,
                                                     &scope_val, &scope_len) ||
        scope_val == nullptr || scope_len == 0) {
        ctx->cache_auth_bypass = true;
        return;
    }

    if (cache::build_private_cache_scope_hash(scope_val, scope_len, ctx->private_cache_scope_hash,
                                              sizeof(ctx->private_cache_scope_hash))) {
        ctx->private_cache_scope_ready = true;
    } else {
        ctx->cache_auth_bypass = true;
    }
}

static void prepare_cache_vary_context(const envoy::config::core::v3::HeaderMap& headers,
                                       const policy::RoutePolicy* pol,
                                       apg::ApgTransformContext* ctx) {
    ctx->cache_vary_count = 0;
    ctx->cache_vary_ready = false;

    if (pol == nullptr || pol->cache.vary_headers.count == 0) {
        ctx->cache_vary_ready = true;
        return;
    }

    for (std::size_t i = 0; i < pol->cache.vary_headers.count; ++i) {
        const char* hdr_name = pol->cache.vary_headers.names[i];
        const char* raw_val = nullptr;
        std::size_t raw_len = 0;

        std::strncpy(ctx->cache_vary_names[i], hdr_name, policy::kMaxCacheVaryHeaderNameLen - 1);

        const bool found =
            extproc::read_header_value_case_insensitive(headers, hdr_name, &raw_val, &raw_len);

        if (found && raw_val != nullptr && raw_len > 0) {
            cache::build_cache_vary_value_hash(raw_val, raw_len, ctx->cache_vary_value_hashes[i],
                                               sizeof(ctx->cache_vary_value_hashes[i]));
        } else if (found) {
            // Header present but empty: stable sentinel
            static constexpr char kEmpty[] = "<empty>";
            cache::build_cache_vary_value_hash(kEmpty, sizeof(kEmpty) - 1,
                                               ctx->cache_vary_value_hashes[i],
                                               sizeof(ctx->cache_vary_value_hashes[i]));
        } else {
            // Missing header: stable sentinel so absent != empty-string
            static constexpr char kMissing[] = "<missing>";
            cache::build_cache_vary_value_hash(kMissing, sizeof(kMissing) - 1,
                                               ctx->cache_vary_value_hashes[i],
                                               sizeof(ctx->cache_vary_value_hashes[i]));
        }
        ++ctx->cache_vary_count;
    }
    ctx->cache_vary_ready = true;
}

RequestHeaderView
apply_request_headers_selection(const envoy::service::ext_proc::v3::ProcessingRequest& request,
                                StreamFilterState* state) {
    if (state == nullptr || !request.has_request_headers()) {
        return {};
    }

    state->context = apg::ApgTransformContext{};
    state->response_kind = json_transform::JsonResponseKind::SkipUnsupported;
    state->has_query_selection = false;

    const auto view = scan_request_headers(request.request_headers().headers());

    if (view.path == nullptr) {
        return view;
    }

    if (!field_selection::extract_raw_path_and_query(view.path, &state->context)) {
        return view;
    }
    apg::parse_query_view(state->context.raw_query, state->context.raw_query_length,
                          &state->context.request_query_view);
    state->context.request_query_view_ready = true;

    if (!field_selection::parse_and_store_selected_fields(&state->context)) {
        return view;
    }

    if (view.method != nullptr) {
        const std::string_view method(view.method, view.method_len);
        if (method == "GET" || method == "get") {
            state->context.request_method = policy::HttpMethod::Get;
        } else if (method == "POST" || method == "post") {
            state->context.request_method = policy::HttpMethod::Post;
        }
    }

    if (view.accept_encoding != nullptr) {
        state->context.client_accept_encoding =
            compression::parse_accept_encoding(view.accept_encoding, view.accept_encoding_len);
    }

    state->has_query_selection = state->context.selected_field_count > 0;
    return view;
}

void apply_response_content_type(const envoy::service::ext_proc::v3::ProcessingRequest& request,
                                 StreamFilterState* state) {
    if (state == nullptr || !request.has_response_headers()) {
        return;
    }

    const auto view = scan_response_headers(request.response_headers().headers());

    if (view.status != nullptr) {
        state->context.response_status_code = static_cast<std::uint16_t>(std::atoi(view.status));
        if (view.status_len > 0 && view.status[0] != '2') {
            state->is_non_2xx_response = true;
            state->response_kind = json_transform::JsonResponseKind::SkipUnsupported;
        }
    }

    if (view.content_encoding != nullptr) {
        state->context.response_content_encoding =
            compression::detect_content_encoding(view.content_encoding, view.content_encoding_len);
    }

    if (view.content_type != nullptr) {
        const std::size_t copy_len =
            std::min(view.content_type_len, sizeof(state->context.response_content_type) - 1);
        std::memcpy(state->context.response_content_type, view.content_type, copy_len);
        state->context.response_content_type[copy_len] = '\0';
        state->context.response_content_type_len = copy_len;
        json_transform::detect_application_json_response(state->context.response_content_type,
                                                         &state->response_kind);
    } else {
        state->response_kind = json_transform::JsonResponseKind::SkipUnsupported;
    }

    if (view.content_length != nullptr) {
        state->context.response_body_len =
            static_cast<std::size_t>(std::atoll(view.content_length));
        state->context.response_body_size_known = true;
    }
}

bool is_compression_only_route(const StreamFilterState& state) {
    if (state.matched_policy == nullptr) {
        return false;
    }
    return state.matched_policy->compression.enabled && !state.has_query_selection &&
           state.matched_policy->cache.behavior != policy::CacheBehavior::Store &&
           !state.matched_policy->pagination.enabled && !state.matched_policy->coalescing.enabled;
}

bool route_needs_response_body_processing(const StreamFilterState& state) {
    if (state.matched_policy == nullptr) {
        return true;
    }

    if (state.has_query_selection) {
        return true;
    }

    if (state.matched_policy->cache.behavior == policy::CacheBehavior::Store) {
        return true;
    }

    if (is_compression_only_route(state)) {
        if (!state.compression_decision_final) {
            return true;
        }
    } else {
        if (!state.context.compression_decision_final) {
            return true;
        }
    }

    return false;
}

static bool write_noop_response_body_continue(
    grpc::ServerReaderWriter<envoy::service::ext_proc::v3::ProcessingResponse,
                             envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
    if (stream == nullptr) {
        return false;
    }
    envoy::service::ext_proc::v3::ProcessingResponse response{};
    auto* response_body = response.mutable_response_body();
    auto* common = response_body->mutable_response();
    common->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
    return stream->Write(response);
}

ReportingHeaderMode reporting_mode_for_route(const StreamFilterState& state) {
    if (state.matched_policy == nullptr) {
        return ReportingHeaderMode::FullDiagnostics;
    }

    return is_compression_only_route(state) ? ReportingHeaderMode::CompressionOnly
                                            : ReportingHeaderMode::FullDiagnostics;
}

bool build_filtered_body_response(const envoy::service::ext_proc::v3::ProcessingRequest& request,
                                  StreamFilterState& state,
                                  envoy::service::ext_proc::v3::ProcessingResponse* response_out,
                                  safety::FailOpenReason* out_reason) {
    if (out_reason != nullptr) {
        *out_reason = safety::FailOpenReason::None;
    }

    if (response_out == nullptr || !request.has_response_body()) {
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::SkipUnsupported;
        }
        return false;
    }
    if (state.matched_policy == nullptr) {
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::PolicyNotFound;
        }
        return false;
    }
    const char* reason_err = nullptr;
    if (!policy::validate_route_policy(*state.matched_policy, &reason_err)) {
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::InvalidPolicy;
        }
        return false;
    }
    if (state.matched_policy->mutation != policy::MutationMode::Full) {
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::ObserveMode;
        }
        return false;
    }
    if (state.is_non_2xx_response) {
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::Non2xxResponse;
        }
        return false;
    }

    const bool filtering_active = state.has_query_selection;

    if (filtering_active) {
        if (state.response_kind != json_transform::JsonResponseKind::EligibleJson) {
            if (out_reason != nullptr) {
                *out_reason = safety::FailOpenReason::NonJsonResponse;
            }
            return false;
        }
    } else {
        if (state.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
            if (out_reason != nullptr) {
                *out_reason = safety::FailOpenReason::SkipUnsupported;
            }
            return false;
        }
    }

    if (!request.response_body().end_of_stream()) {
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::SkipUnsupported;
        }
        return false;
    }

    const std::string& input_body = request.response_body().body();
    if (state.matched_policy != nullptr &&
        policy::exceeds_max_response_bytes(*state.matched_policy, input_body.size())) {
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::PayloadTooLarge;
        }
        return false;
    }

    if (!filtering_active) {
        auto* body_response = response_out->mutable_response_body();
        auto* common = body_response->mutable_response();
        common->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
        add_overwrite_header(common, kContentLengthHeader, std::to_string(input_body.size()));
        add_bytetaper_report_headers(common, input_body.size(), input_body.size(), 0, 0, false,
                                     state.context.cache_hit);
        state.context.output_payload_bytes = input_body.size();
        return true;
    }

    state.context.input_payload_bytes = input_body.size();
    json_transform::ParsedFlatJsonObject parsed{};
    const json_transform::FlatJsonParseStatus parse_status =
        json_transform::parse_flat_json_object(input_body.c_str(), state.response_kind, &parsed);

    std::vector<char> output(input_body.size() + 1, '\0');
    std::size_t output_length = 0;
    const json_transform::FlatJsonFilterStatus status =
        json_transform::transform_flat_json_with_filter_toggle(
            input_body.c_str(), parse_status, &parsed, state.context, true, output.data(),
            output.size(), &output_length);

    const safety::FailOpenDecision safety_decision = safety::evaluate_filter_safety(status);
    if (out_reason != nullptr) {
        *out_reason = safety_decision.reason;
    }

    if (!safety_decision.should_mutate) {
        return false;
    }
    std::string filtered_body;
    filtered_body.assign(output.data(), output_length);
    std::size_t saved_bytes = (input_body.size() >= filtered_body.size())
                                  ? (input_body.size() - filtered_body.size())
                                  : 0;

    auto* body_response = response_out->mutable_response_body();
    auto* common = body_response->mutable_response();
    common->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
    common->mutable_body_mutation()->set_body(filtered_body);

    add_overwrite_header(common, kContentLengthHeader, std::to_string(filtered_body.size()));
    add_bytetaper_report_headers(common, input_body.size(), filtered_body.size(),
                                 state.context.removed_field_count, saved_bytes, true,
                                 state.context.cache_hit);
    state.context.output_payload_bytes = filtered_body.size();

    return true;
}

class ExternalProcessorSkeletonService final
    : public envoy::service::ext_proc::v3::ExternalProcessor::Service {
public:
    const policy::RoutePolicy* policies = nullptr;
    std::size_t policy_count = 0;
    CompiledRouteRuntimeTable route_runtimes{};
    cache::L1Cache* l1_cache = nullptr;
    cache::L2DiskCache* l2_cache = nullptr;
    metrics::MetricsRegistry* metrics_registry = nullptr;
    coalescing::InFlightRegistry* coalescing_registry = nullptr;
    std::unique_ptr<runtime::WorkerQueue> worker_queue;
    policy::CompiledRouteMatcher route_matcher{};
    bool route_matcher_ready = false;

    grpc::Status Process(grpc::ServerContext*,
                         grpc::ServerReaderWriter<envoy::service::ext_proc::v3::ProcessingResponse,
                                                  envoy::service::ext_proc::v3::ProcessingRequest>*
                             stream) override {
        if (stream == nullptr) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "missing stream");
        }

        ProcessingStreamStats stream_stats{};
        StreamFilterState filter_state{};

        bool trace_enabled = observability::trace_global_config().enabled;
        observability::TraceSpanScope root_span{};
        if (trace_enabled) {
            const char* scenario_env = std::getenv("BYTETAPER_TRACE_SCENARIO");
            observability::trace_start_record(
                &filter_state.trace, scenario_env ? scenario_env : "compression_coordination");

            root_span =
                observability::trace_start_span(&filter_state.trace, "bytetaper.extproc.stream",
                                                observability::TraceLatencyClass::ActiveProcessing,
                                                &filter_state.trace.root_span_id);
        }

        envoy::service::ext_proc::v3::ProcessingRequest request{};
        while (true) {
            observability::TraceSpanScope read_span{};
            if (trace_enabled) {
                read_span = observability::trace_start_span(
                    &filter_state.trace, "bytetaper.grpc.read.wait",
                    observability::TraceLatencyClass::GrpcReadWait, &root_span.span->span_id);
            }
            bool has_message = stream->Read(&request);
            if (trace_enabled && read_span.span != nullptr) {
                read_span.end();
                if (!has_message) {
                    observability::trace_rename_span(read_span.span,
                                                     "bytetaper.grpc.read.wait.stream_close");
                }
            }
            if (!has_message) {
                break;
            }

            if (metrics_registry != nullptr) {
                metrics::record_stream(metrics_registry, {}); // placeholder for stream count
            }
            const ProcessingRequestKind kind =
                classify_request_kind(request.has_request_headers(), request.has_response_headers(),
                                      request.has_response_body());
            record_request_kind(kind, &stream_stats);

            if (trace_enabled && read_span.span != nullptr) {
                const char* read_wait_name = (kind == ProcessingRequestKind::RequestHeaders)
                                                 ? "bytetaper.grpc.read.wait.request_headers"
                                             : (kind == ProcessingRequestKind::ResponseHeaders)
                                                 ? "bytetaper.grpc.read.wait.response_headers"
                                             : (kind == ProcessingRequestKind::ResponseBody)
                                                 ? "bytetaper.grpc.read.wait.response_body"
                                                 : "bytetaper.grpc.read.wait.unknown";
                observability::trace_rename_span(read_span.span, read_wait_name);
            }

            if (kind == ProcessingRequestKind::RequestHeaders) {
                const auto req_view = apply_request_headers_selection(request, &filter_state);
                filter_state.matched_policy = nullptr;
                if (route_matcher_ready && filter_state.context.raw_path_length > 0) {
                    filter_state.matched_policy =
                        policy::match_route_compiled(route_matcher, filter_state.context.raw_path);
                }

                if (trace_enabled) {
                    const char* r_id = "default";
                    if (filter_state.matched_policy != nullptr) {
                        r_id = filter_state.matched_policy->route_id;
                    }
                    observability::trace_set_route(&filter_state.trace, r_id,
                                                   filter_state.context.raw_path);
                }

                observability::TraceSpanScope req_hdrs_span{};
                if (trace_enabled) {
                    req_hdrs_span = observability::trace_start_span(
                        &filter_state.trace, "bytetaper.active.request_headers",
                        observability::TraceLatencyClass::ActiveProcessing,
                        &root_span.span->span_id);
                }

                envoy::service::ext_proc::v3::ProcessingResponse response{};

                // Run lookup pipeline
                if (filter_state.matched_policy != nullptr) {
                    filter_state.context.matched_policy = filter_state.matched_policy;
                    prepare_cache_auth_context(request.request_headers().headers(), req_view,
                                               &filter_state.context);
                    prepare_cache_vary_context(request.request_headers().headers(),
                                               filter_state.matched_policy, &filter_state.context);
                    filter_state.context.l1_cache = l1_cache;
                    filter_state.context.l2_cache = l2_cache;
                    filter_state.context.coalescing_registry = coalescing_registry;
                    filter_state.context.request_epoch_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

                    // Execution boundary: hot-path only. See docs/runtime/RUNTIME_BOUNDARIES.md.
                    if (metrics_registry != nullptr) {
                        filter_state.context.pagination_metrics =
                            &metrics_registry->pagination_metrics;
                        filter_state.context.cache_metrics = &metrics_registry->cache_metrics;
                        filter_state.context.compression_metrics =
                            &metrics_registry->compression_metrics;
                        filter_state.context.coalescing_metrics =
                            &metrics_registry->coalescing_metrics;
                        filter_state.context.runtime_metrics = &metrics_registry->runtime_metrics;
                    }
                    filter_state.context.worker_queue = worker_queue.get();

                    observability::TraceSpanScope cache_lookup_span{};
                    if (trace_enabled) {
                        cache_lookup_span = observability::trace_start_span(
                            &filter_state.trace, "bytetaper.cache.l1.lookup",
                            observability::TraceLatencyClass::CacheIo,
                            &req_hdrs_span.span->span_id);
                    }

                    const auto* route_runtime =
                        find_compiled_route_runtime(route_runtimes, filter_state.matched_policy);
                    if (route_runtime != nullptr) {
                        apg::run_pipeline(route_runtime->lookup_stages, route_runtime->lookup_count,
                                          filter_state.context);
                    } else {
                        apg::run_pipeline(extproc::kLookupStages, extproc::kLookupStageCount,
                                          filter_state.context);
                    }

                    if (trace_enabled) {
                        cache_lookup_span.end();
                    }
                }

                if (bytetaper::extproc::map_cache_hit_to_immediate_response(filter_state.context,
                                                                            &response)) {
                    observability::TraceSpanScope write_immediate_span{};
                    if (trace_enabled) {
                        req_hdrs_span.end();
                        write_immediate_span = observability::trace_start_span(
                            &filter_state.trace, "bytetaper.grpc.write.immediate_response",
                            observability::TraceLatencyClass::GrpcWrite, &root_span.span->span_id);
                    }
                    stream->Write(response);
                    if (trace_enabled) {
                        write_immediate_span.end();
                    }
                    continue; // skip all further processing for this request
                }

                auto* request_headers_response = response.mutable_request_headers();
                auto* common = request_headers_response->mutable_response();
                common->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
                apply_pagination_request_headers(filter_state.context, common);

                observability::TraceSpanScope write_req_hdrs_span{};
                if (trace_enabled) {
                    req_hdrs_span.end();
                    write_req_hdrs_span = observability::trace_start_span(
                        &filter_state.trace, "bytetaper.grpc.write.request_headers",
                        observability::TraceLatencyClass::GrpcWrite, &root_span.span->span_id);
                }

                stream->Write(response);

                if (trace_enabled) {
                    write_req_hdrs_span.end();
                }
                continue;
            }
            if (kind == ProcessingRequestKind::ResponseHeaders) {
                apply_response_content_type(request, &filter_state);

                observability::TraceSpanScope resp_hdrs_span{};
                if (trace_enabled) {
                    resp_hdrs_span = observability::trace_start_span(
                        &filter_state.trace, "bytetaper.active.response_headers",
                        observability::TraceLatencyClass::ActiveProcessing,
                        &root_span.span->span_id);
                }
                envoy::service::ext_proc::v3::ProcessingResponse response{};
                auto* response_headers = response.mutable_response_headers();
                auto* common_response = response_headers->mutable_response();
                common_response->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
                const ReportingHeaderMode reporting_mode = reporting_mode_for_route(filter_state);
                if (reporting_mode == ReportingHeaderMode::FullDiagnostics) {
                    add_bytetaper_report_headers(common_response, 0, 0, 0, 0, false,
                                                 filter_state.context.cache_hit);
                    const char* route_id = (filter_state.matched_policy != nullptr)
                                               ? filter_state.matched_policy->route_id
                                               : kNoneValue;
                    add_overwrite_header(common_response, kRoutePolicyHeader, route_id);
                }

                // Run compression decision pipeline during headers for early signaling (handles
                // HEAD etc)
                if (filter_state.matched_policy != nullptr) {
                    if (is_compression_only_route(filter_state)) {
                        filter_state.compression_context.matched_policy =
                            filter_state.matched_policy;
                        filter_state.compression_context.client_accept_encoding =
                            filter_state.context.client_accept_encoding;
                        filter_state.compression_context.response_content_encoding =
                            filter_state.context.response_content_encoding;
                        filter_state.compression_context.response_status_code =
                            filter_state.context.response_status_code;

                        std::memcpy(filter_state.owned_content_type,
                                    filter_state.context.response_content_type,
                                    sizeof(filter_state.owned_content_type));
                        filter_state.compression_context.response_content_type =
                            filter_state.owned_content_type;
                        filter_state.compression_context.response_content_type_len =
                            filter_state.context.response_content_type_len;

                        filter_state.compression_context.response_body_len =
                            filter_state.context.response_body_len;
                        filter_state.compression_context.response_body_size_known =
                            filter_state.context.response_body_size_known;
                        filter_state.compression_context.compression_metrics =
                            filter_state.context.compression_metrics;

                        observability::TraceSpanScope eval_span{};
                        if (trace_enabled) {
                            eval_span = observability::trace_start_span(
                                &filter_state.trace, "bytetaper.compression.decision",
                                observability::TraceLatencyClass::ActiveProcessingDetail,
                                &resp_hdrs_span.span->span_id);
                        }

                        stages::evaluate_compression_decision_fast(
                            filter_state.compression_context);

                        if (trace_enabled) {
                            eval_span.end();
                        }

                        if (filter_state.compression_context.response_body_size_known &&
                            filter_state.compression_context.compression_decision.evaluated) {
                            filter_state.compression_decision_final = true;
                        }

                        if (filter_state.compression_decision_final) {
                            observability::TraceSpanScope mutation_span{};
                            if (trace_enabled) {
                                mutation_span = observability::trace_start_span(
                                    &filter_state.trace, "bytetaper.header_mutation",
                                    observability::TraceLatencyClass::ActiveProcessingDetail,
                                    &resp_hdrs_span.span->span_id);
                            }

                            apply_compression_decision_headers(
                                filter_state.compression_context.compression_decision,
                                common_response);

                            if (trace_enabled) {
                                mutation_span.end();
                            }
                        }
                    } else {
                        const auto* route_runtime = find_compiled_route_runtime(
                            route_runtimes, filter_state.matched_policy);
                        if (route_runtime != nullptr) {
                            apg::run_pipeline(route_runtime->response_stages,
                                              route_runtime->response_count, filter_state.context);
                        } else {
                            static constexpr apg::ApgStage kCompressionStages[] = {
                                stages::compression_decision_stage
                            };
                            apg::run_pipeline(kCompressionStages, 1, filter_state.context);
                        }
                        if (filter_state.context.response_body_size_known &&
                            filter_state.context.compression_decision.evaluated) {
                            filter_state.context.compression_decision_final = true;
                        }
                        apply_compression_response_headers(filter_state.context, common_response);
                    }
                }

                apply_pagination_response_headers(filter_state.context, common_response);

                observability::TraceSpanScope write_resp_hdrs_span{};
                if (trace_enabled) {
                    resp_hdrs_span.end();
                    write_resp_hdrs_span = observability::trace_start_span(
                        &filter_state.trace, "bytetaper.grpc.write.response_headers",
                        observability::TraceLatencyClass::GrpcWrite, &root_span.span->span_id);
                }

                stream->Write(response);

                if (trace_enabled) {
                    write_resp_hdrs_span.end();
                }
                continue;
            }
            if (kind == ProcessingRequestKind::ResponseBody) {
                if (is_compression_only_route(filter_state)) {
                    observability::TraceSpanScope resp_body_span{};
                    if (trace_enabled) {
                        resp_body_span = observability::trace_start_span(
                            &filter_state.trace, "bytetaper.active.response_body",
                            observability::TraceLatencyClass::ActiveProcessing,
                            &root_span.span->span_id);
                    }

                    if (filter_state.compression_decision_final) {
                        observability::TraceSpanScope write_continue_span{};
                        if (trace_enabled) {
                            resp_body_span.end();
                            write_continue_span = observability::trace_start_span(
                                &filter_state.trace, "bytetaper.grpc.write.response_body_continue",
                                observability::TraceLatencyClass::GrpcWrite,
                                &root_span.span->span_id);
                        }

                        write_noop_response_body_continue(stream);

                        if (trace_enabled) {
                            write_continue_span.end();
                        }
                        continue;
                    }

                    const std::string& input_body = request.response_body().body();
                    filter_state.compression_context.response_body_len = input_body.size();
                    filter_state.compression_context.response_body_size_known = true;

                    stages::evaluate_compression_decision_fast(filter_state.compression_context);
                    filter_state.compression_decision_final = true;

                    envoy::service::ext_proc::v3::ProcessingResponse response{};
                    auto* response_body = response.mutable_response_body();
                    auto* common = response_body->mutable_response();
                    common->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);

                    // Add content-length header
                    add_overwrite_header(common, kContentLengthHeader,
                                         std::to_string(input_body.size()));

                    // Write compression decision headers
                    apply_compression_decision_headers(
                        filter_state.compression_context.compression_decision, common);

                    observability::TraceSpanScope write_continue_span{};
                    if (trace_enabled) {
                        resp_body_span.end();
                        write_continue_span = observability::trace_start_span(
                            &filter_state.trace, "bytetaper.grpc.write.response_body_continue",
                            observability::TraceLatencyClass::GrpcWrite, &root_span.span->span_id);
                    }

                    stream->Write(response);

                    if (trace_enabled) {
                        write_continue_span.end();
                    }
                    continue;
                }

                observability::TraceSpanScope resp_body_span{};
                if (trace_enabled) {
                    resp_body_span = observability::trace_start_span(
                        &filter_state.trace, "bytetaper.active.response_body",
                        observability::TraceLatencyClass::ActiveProcessing,
                        &root_span.span->span_id);
                }

                if (!route_needs_response_body_processing(filter_state)) {
                    observability::TraceSpanScope write_continue_span{};
                    if (trace_enabled) {
                        resp_body_span.end();
                        write_continue_span = observability::trace_start_span(
                            &filter_state.trace, "bytetaper.grpc.write.response_body_continue",
                            observability::TraceLatencyClass::GrpcWrite, &root_span.span->span_id);
                    }
                    write_noop_response_body_continue(stream);
                    if (trace_enabled) {
                        write_continue_span.end();
                    }
                    continue;
                }

                envoy::service::ext_proc::v3::ProcessingResponse response{};

                safety::FailOpenReason fail_reason = safety::FailOpenReason::None;
                if (!build_filtered_body_response(request, filter_state, &response, &fail_reason)) {
                    if (filter_state.matched_policy != nullptr &&
                        filter_state.matched_policy->failure_mode ==
                            policy::FailureMode::FailClosed &&
                        fail_reason != safety::FailOpenReason::None &&
                        fail_reason != safety::FailOpenReason::SkipUnsupported) {
                        auto* immediate = response.mutable_immediate_response();
                        immediate->mutable_status()->set_code(
                            envoy::type::v3::StatusCode::InternalServerError);
                        immediate->set_details("bytetaper_fail_closed");
                        immediate->set_body("ByteTaper safety constraint triggered (fail-closed)");
                        auto* headers = immediate->mutable_headers();
                        auto* mutation = headers->add_set_headers();
                        mutation->mutable_header()->set_key("x-bytetaper-fail-closed-reason");
                        mutation->mutable_header()->set_raw_value(
                            safety::get_fail_open_reason_string(fail_reason));
                        mutation->set_append_action(
                            envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
                    } else {
                        auto* response_body = response.mutable_response_body();
                        auto* common_response = response_body->mutable_response();
                        common_response->set_status(
                            envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
                        add_bytetaper_report_headers(common_response,
                                                     request.response_body().body().size(),
                                                     request.response_body().body().size(), 0, 0,
                                                     false, filter_state.context.cache_hit);
                        if (fail_reason != safety::FailOpenReason::None) {
                            add_overwrite_header(common_response, "x-bytetaper-fail-open-reason",
                                                 safety::get_fail_open_reason_string(fail_reason));
                        }
                    }
                } else {
                    // Success! Store in cache if enabled.
                    if (filter_state.matched_policy != nullptr &&
                        filter_state.matched_policy->cache.behavior ==
                            policy::CacheBehavior::Store) {

                        const std::string& body_to_store =
                            filter_state.has_query_selection
                                ? response.response_body().response().body_mutation().body()
                                : request.response_body().body();
                        filter_state.context.response_body = body_to_store.c_str();
                        filter_state.context.response_body_len = body_to_store.size();

                        // Execution boundary: hot-path only. See
                        // docs/runtime/RUNTIME_BOUNDARIES.md.
                        observability::TraceSpanScope cache_store_span{};
                        if (trace_enabled) {
                            cache_store_span = observability::trace_start_span(
                                &filter_state.trace, "bytetaper.cache.store",
                                observability::TraceLatencyClass::CacheIo,
                                &resp_body_span.span->span_id);
                        }

                        const auto* route_runtime = find_compiled_route_runtime(
                            route_runtimes, filter_state.matched_policy);
                        if (route_runtime != nullptr) {
                            apg::run_pipeline(route_runtime->store_stages,
                                              route_runtime->store_count, filter_state.context);
                        } else {
                            apg::run_pipeline(extproc::kStoreStages, extproc::kStoreStageCount,
                                              filter_state.context);
                        }

                        if (trace_enabled) {
                            cache_store_span.end();
                        }
                    }
                }

                // Run compression decision pipeline again during body if needed (refines decision
                // with actual body size)
                if (filter_state.matched_policy != nullptr &&
                    !filter_state.context.compression_decision_final) {
                    if (!filter_state.context.response_body_size_known) {
                        filter_state.context.response_body_len =
                            request.response_body().body().size();
                        filter_state.context.response_body_size_known = true;
                    }
                    const auto* route_runtime =
                        find_compiled_route_runtime(route_runtimes, filter_state.matched_policy);
                    if (route_runtime != nullptr) {
                        apg::run_pipeline(route_runtime->response_stages,
                                          route_runtime->response_count, filter_state.context);
                    } else {
                        static constexpr apg::ApgStage kCompressionStages[] = {
                            stages::compression_decision_stage
                        };
                        apg::run_pipeline(kCompressionStages, 1, filter_state.context);
                    }

                    if (response.has_response_body()) {
                        apply_compression_response_headers(
                            filter_state.context,
                            response.mutable_response_body()->mutable_response());
                    }
                }

                observability::TraceSpanScope write_resp_body_span{};
                if (trace_enabled) {
                    resp_body_span.end();
                    write_resp_body_span = observability::trace_start_span(
                        &filter_state.trace, "bytetaper.grpc.write.response_body",
                        observability::TraceLatencyClass::GrpcWrite, &root_span.span->span_id);
                }

                stream->Write(response);

                if (trace_enabled) {
                    write_resp_body_span.end();
                }
                continue;
            }
        }

        if (trace_enabled && root_span.span != nullptr) {
            root_span.end();
            filter_state.trace.total_duration_nano = root_span.span->duration_nano;

            // Set generic information
            if (is_compression_only_route(filter_state)) {
                observability::trace_set_compression_info(
                    &filter_state.trace,
                    filter_state.compression_context.compression_decision.candidate,
                    filter_state.compression_decision_final,
                    filter_state.compression_context.response_body_len,
                    filter_state.compression_context.response_body_size_known);
                filter_state.trace.response_status_code =
                    filter_state.compression_context.response_status_code;
            } else {
                observability::trace_set_compression_info(
                    &filter_state.trace, filter_state.context.compression_decision.candidate,
                    filter_state.context.compression_decision_final,
                    filter_state.context.response_body_len,
                    filter_state.context.response_body_size_known);
                filter_state.trace.response_status_code = filter_state.context.response_status_code;
                observability::trace_set_cache_info(&filter_state.trace,
                                                    filter_state.context.cache_hit);

                // Set coalescing info
                bool leader = (filter_state.context.coalescing_decision.action ==
                               bytetaper::coalescing::CoalescingAction::Leader);
                bool follower = (filter_state.context.coalescing_decision.action ==
                                 bytetaper::coalescing::CoalescingAction::Follower);
                observability::trace_set_coalescing_info(&filter_state.trace, leader, follower);
            }

            // Run classification
            observability::trace_classify(&filter_state.trace);

            const auto& trace_config = observability::trace_global_config();
            bool should_save = false;

            if (std::strcmp(trace_config.mode, "all") == 0) {
                should_save = true;
            } else if (std::strcmp(trace_config.mode, "slow") == 0) {
                should_save = observability::trace_is_slow(filter_state.trace, trace_config);
            } else if (std::strcmp(trace_config.mode, "sampled") == 0) {
                should_save = observability::trace_is_sampled(trace_config);
            }

            if (should_save) {
                observability::trace_push(observability::trace_global_ring(), trace_config,
                                          filter_state.trace);
            }
        }

        (void) stream_stats;
        return grpc::Status::OK;
    }
};

struct GrpcServerImpl {
    ExternalProcessorSkeletonService service{};
    std::unique_ptr<grpc::Server> server{};
};

struct WorkerQueueStartGuard {
    runtime::WorkerQueue* queue = nullptr;
    bool active = false;

    ~WorkerQueueStartGuard() {
        if (active && queue != nullptr) {
            runtime::worker_queue_shutdown(queue);
        }
    }

    void arm(runtime::WorkerQueue* q) {
        queue = q;
        active = true;
    }

    void release() {
        active = false;
    }
};

std::size_t derive_async_store_max_body_size(const policy::RoutePolicy* policies,
                                             std::size_t policy_count) {
    std::size_t max_body_size = 0;
    bool has_unlimited_policy = false;
    if (policies == nullptr) {
        return runtime::kAsyncL2StoreDefaultMaxBodySize;
    }
    for (std::size_t i = 0; i < policy_count; ++i) {
        const std::uint32_t route_limit = policies[i].max_response_bytes;
        if (route_limit == 0) {
            has_unlimited_policy = true;
            continue;
        }
        std::size_t capped_limit = route_limit;
        if (capped_limit > runtime::kAsyncL2StoreAbsoluteMaxBodySize) {
            capped_limit = runtime::kAsyncL2StoreAbsoluteMaxBodySize;
        }
        if (capped_limit > max_body_size) {
            max_body_size = capped_limit;
        }
    }
    if (max_body_size == 0) {
        return runtime::kAsyncL2StoreDefaultMaxBodySize;
    }
    if (has_unlimited_policy && max_body_size < runtime::kAsyncL2StoreDefaultMaxBodySize) {
        return runtime::kAsyncL2StoreDefaultMaxBodySize;
    }
    return max_body_size;
}

} // namespace

bool start_grpc_server(const GrpcServerConfig& config, GrpcServerHandle* handle) {
    if (handle == nullptr) {
        return false;
    }
    observability::trace_init(observability::trace_config_from_env());
    if (handle->impl != nullptr) {
        return false;
    }
    if (config.listen_address == nullptr) {
        return false;
    }

    auto impl = std::make_unique<GrpcServerImpl>();

    grpc::ServerBuilder builder{};
    int selected_port = 0;
    builder.AddListeningPort(config.listen_address, grpc::InsecureServerCredentials(),
                             &selected_port);
    builder.RegisterService(&impl->service);

    impl->service.policies = config.policies;
    impl->service.policy_count = config.policy_count;
    compile_route_runtime_table(config.policies, config.policy_count,
                                &impl->service.route_runtimes);
    if (config.policies != nullptr && config.policy_count > 0) {
        policy::compile_route_matcher(config.policies, config.policy_count,
                                      &impl->service.route_matcher);
        impl->service.route_matcher_ready = true;
    }
    impl->service.l1_cache = config.l1_cache;
    impl->service.l2_cache = config.l2_cache;
    impl->service.metrics_registry = config.metrics_registry;
    impl->service.coalescing_registry = config.coalescing_registry;

    // Allocate worker queue on heap and handle OOM cleanly
    impl->service.worker_queue.reset(new (std::nothrow) runtime::WorkerQueue{});
    if (!impl->service.worker_queue) {
        return false;
    }

    // Initialize background worker resources
    runtime::WorkerQueueConfig wq_config{};
    wq_config.worker_count = 2;
    wq_config.async_store_max_body_size =
        derive_async_store_max_body_size(config.policies, config.policy_count);
    const char* wq_err = runtime::worker_queue_init(impl->service.worker_queue.get(), wq_config);
    if (wq_err != nullptr) {
        return false;
    }

    runtime::WorkerQueueResources wq_res{};
    wq_res.l1_cache = config.l1_cache;
    wq_res.l2_cache = config.l2_cache;
    wq_res.runtime_metrics =
        config.metrics_registry ? &config.metrics_registry->runtime_metrics : nullptr;
    wq_res.coalescing_metrics =
        config.metrics_registry ? &config.metrics_registry->coalescing_metrics : nullptr;
    WorkerQueueStartGuard worker_guard{};
    wq_err = runtime::worker_queue_start(impl->service.worker_queue.get(), wq_res);
    if (wq_err != nullptr) {
        return false;
    }
    worker_guard.arm(impl->service.worker_queue.get());

    impl->server = builder.BuildAndStart();
    if (!impl->server) {
        return false;
    }
    if (selected_port <= 0 || selected_port > 65535) {
        impl->server->Shutdown();
        impl->server->Wait();
        return false;
    }

    handle->bound_port = static_cast<std::uint16_t>(selected_port);
    handle->impl = impl.release();
    worker_guard.release();
    return true;
}

void stop_grpc_server(GrpcServerHandle* handle) {
    if (handle == nullptr) {
        return;
    }
    if (handle->impl == nullptr) {
        handle->bound_port = 0;
        return;
    }

    observability::trace_flush(observability::trace_global_ring(),
                               observability::trace_global_config(),
                               std::getenv("BYTETAPER_TRACE_SCENARIO"));

    auto* impl = static_cast<GrpcServerImpl*>(handle->impl);
    if (impl->server) {
        impl->server->Shutdown();
        impl->server->Wait();
    }

    runtime::worker_queue_shutdown(impl->service.worker_queue.get());

    delete impl;
    handle->impl = nullptr;
    handle->bound_port = 0;
}

} // namespace bytetaper::extproc

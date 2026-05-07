// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/grpc_server.h"

#include "apg/pipeline.h"
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
#include "policy/route_matcher.h"
#include "runtime/worker_queue.h"
#include "safety/fail_open.h"
#include "stages/compression_decision_stage.h"
#include "stages/pagination_request_mutation_stage.h"

#include <chrono>
#include <cstddef>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <vector>

namespace bytetaper::extproc {

namespace {

constexpr const char* kPathHeader = ":path";
constexpr const char* kContentTypeHeader = "content-type";
constexpr const char* kContentLengthHeader = "content-length";
constexpr const char* kResponseBodyHeader = "x-bytetaper-extproc-response-body";
constexpr const char* kOriginalResponseBytesHeader = "x-bytetaper-original-response-bytes";
constexpr const char* kWasteRemovedFieldsHeader = "x-bytetaper-waste-removed-fields";
constexpr const char* kWasteSavedBytesHeader = "x-bytetaper-waste-saved-bytes";
constexpr const char* kOptimizedResponseBytesHeader = "x-bytetaper-optimized-response-bytes";
constexpr const char* kOriginalBytesHeader = "x-bytetaper-original-bytes";
constexpr const char* kOptimizedBytesHeader = "x-bytetaper-optimized-bytes";
constexpr const char* kTransformAppliedHeader = "x-bytetaper-transform-applied";
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

void apply_request_headers_selection(const envoy::service::ext_proc::v3::ProcessingRequest& request,
                                     StreamFilterState* state) {
    if (state == nullptr || !request.has_request_headers()) {
        return;
    }

    state->context = apg::ApgTransformContext{};
    state->response_kind = json_transform::JsonResponseKind::SkipUnsupported;
    state->has_query_selection = false;

    const auto view = scan_request_headers(request.request_headers().headers());

    if (view.path == nullptr) {
        return;
    }

    if (!field_selection::extract_raw_path_and_query(view.path, &state->context)) {
        return;
    }
    apg::parse_query_view(state->context.raw_query, state->context.raw_query_length,
                          &state->context.request_query_view);
    state->context.request_query_view_ready = true;

    if (!field_selection::parse_and_store_selected_fields(&state->context)) {
        return;
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
        std::strncpy(state->context.response_content_type, view.content_type,
                     sizeof(state->context.response_content_type) - 1);
        state->context.response_content_type[sizeof(state->context.response_content_type) - 1] =
            '\0';
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

    if (!state.context.compression_decision_final) {
        return true;
    }

    return false;
}

ReportingHeaderMode reporting_mode_for_route(const StreamFilterState& state) {
    if (state.matched_policy == nullptr) {
        return ReportingHeaderMode::FullDiagnostics;
    }

    const bool compression_only =
        state.matched_policy->compression.enabled && !state.has_query_selection &&
        state.matched_policy->cache.behavior != policy::CacheBehavior::Store &&
        !state.matched_policy->pagination.enabled && !state.matched_policy->coalescing.enabled;

    return compression_only ? ReportingHeaderMode::CompressionOnly
                            : ReportingHeaderMode::FullDiagnostics;
}

bool build_filtered_body_response(const envoy::service::ext_proc::v3::ProcessingRequest& request,
                                  StreamFilterState& state,
                                  envoy::service::ext_proc::v3::ProcessingResponse* response_out,
                                  safety::FailOpenReason* out_reason) {
    std::printf("[BODY RESP] Entering build_filtered_body_response...\n");
    if (out_reason != nullptr) {
        *out_reason = safety::FailOpenReason::None;
    }

    if (response_out == nullptr || !request.has_response_body()) {
        std::printf("[BODY RESP] Failed: no response body\n");
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::SkipUnsupported;
        }
        return false;
    }
    if (state.matched_policy == nullptr) {
        std::printf("[BODY RESP] Failed: no matched policy\n");
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::PolicyNotFound;
        }
        return false;
    }
    const char* reason_err = nullptr;
    if (!policy::validate_route_policy(*state.matched_policy, &reason_err)) {
        std::printf("[BODY RESP] Failed: invalid policy reason=%s\n",
                    reason_err ? reason_err : "unknown");
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::InvalidPolicy;
        }
        return false;
    }
    if (state.matched_policy->mutation != policy::MutationMode::Full) {
        std::printf("[BODY RESP] Failed: observe mode (not full)\n");
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::ObserveMode;
        }
        return false;
    }
    if (state.is_non_2xx_response) {
        std::printf("[BODY RESP] Failed: non 2xx\n");
        if (out_reason != nullptr) {
            *out_reason = safety::FailOpenReason::Non2xxResponse;
        }
        return false;
    }

    const bool filtering_active = state.has_query_selection;

    if (filtering_active) {
        if (state.response_kind != json_transform::JsonResponseKind::EligibleJson) {
            std::printf("[BODY RESP] Failed: non json response in filtering mode\n");
            if (out_reason != nullptr) {
                *out_reason = safety::FailOpenReason::NonJsonResponse;
            }
            return false;
        }
    } else {
        if (state.matched_policy->cache.behavior != policy::CacheBehavior::Store) {
            std::printf("[BODY RESP] Failed: cache behavior not store\n");
            if (out_reason != nullptr) {
                *out_reason = safety::FailOpenReason::SkipUnsupported;
            }
            return false;
        }
    }

    if (!request.response_body().end_of_stream()) {
        std::printf("[BODY RESP] Failed: not end of stream\n");
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
    runtime::WorkerQueue worker_queue{};

    grpc::Status Process(grpc::ServerContext*,
                         grpc::ServerReaderWriter<envoy::service::ext_proc::v3::ProcessingResponse,
                                                  envoy::service::ext_proc::v3::ProcessingRequest>*
                             stream) override {
        if (stream == nullptr) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "missing stream");
        }

        ProcessingStreamStats stream_stats{};
        StreamFilterState filter_state{};
        envoy::service::ext_proc::v3::ProcessingRequest request{};
        while (stream->Read(&request)) {
            if (metrics_registry != nullptr) {
                metrics::record_stream(metrics_registry, {}); // placeholder for stream count
            }
            const ProcessingRequestKind kind =
                classify_request_kind(request.has_request_headers(), request.has_response_headers(),
                                      request.has_response_body());
            record_request_kind(kind, &stream_stats);

            if (kind == ProcessingRequestKind::RequestHeaders) {
                apply_request_headers_selection(request, &filter_state);
                filter_state.matched_policy = nullptr;
                if (policies != nullptr && filter_state.context.raw_path_length > 0) {
                    filter_state.matched_policy = policy::match_route_by_path(
                        policies, policy_count, filter_state.context.raw_path);
                }

                envoy::service::ext_proc::v3::ProcessingResponse response{};

                // Run lookup pipeline
                if (filter_state.matched_policy != nullptr) {
                    filter_state.context.matched_policy = filter_state.matched_policy;
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
                    filter_state.context.worker_queue = &worker_queue;
                    const auto* route_runtime =
                        find_compiled_route_runtime(route_runtimes, filter_state.matched_policy);
                    if (route_runtime != nullptr) {
                        apg::run_pipeline(route_runtime->lookup_stages, route_runtime->lookup_count,
                                          filter_state.context);
                    } else {
                        apg::run_pipeline(extproc::kLookupStages, extproc::kLookupStageCount,
                                          filter_state.context);
                    }
                }

                if (bytetaper::extproc::map_cache_hit_to_immediate_response(filter_state.context,
                                                                            &response)) {
                    stream->Write(response);
                    continue; // skip all further processing for this request
                }

                auto* request_headers_response = response.mutable_request_headers();
                auto* common = request_headers_response->mutable_response();
                common->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
                apply_pagination_request_headers(filter_state.context, common);
                stream->Write(response);
                continue;
            }
            if (kind == ProcessingRequestKind::ResponseHeaders) {
                apply_response_content_type(request, &filter_state);
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
                    if (filter_state.context.response_body_size_known &&
                        filter_state.context.compression_decision.evaluated) {
                        filter_state.context.compression_decision_final = true;
                    }
                    apply_compression_response_headers(filter_state.context, common_response);
                }

                apply_pagination_response_headers(filter_state.context, common_response);
                stream->Write(response);
                continue;
            }
            if (kind == ProcessingRequestKind::ResponseBody) {
                if (!route_needs_response_body_processing(filter_state)) {
                    envoy::service::ext_proc::v3::ProcessingResponse response{};
                    auto* response_body = response.mutable_response_body();
                    auto* common = response_body->mutable_response();
                    common->set_status(envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
                    stream->Write(response);
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
                        const auto* route_runtime = find_compiled_route_runtime(
                            route_runtimes, filter_state.matched_policy);
                        if (route_runtime != nullptr) {
                            apg::run_pipeline(route_runtime->store_stages,
                                              route_runtime->store_count, filter_state.context);
                        } else {
                            apg::run_pipeline(extproc::kStoreStages, extproc::kStoreStageCount,
                                              filter_state.context);
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

                stream->Write(response);
                continue;
            }
            // Non-supported variants are safe no-op.
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

} // namespace

bool start_grpc_server(const GrpcServerConfig& config, GrpcServerHandle* handle) {
    if (handle == nullptr) {
        return false;
    }
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
    impl->service.l1_cache = config.l1_cache;
    impl->service.l2_cache = config.l2_cache;
    impl->service.metrics_registry = config.metrics_registry;
    impl->service.coalescing_registry = config.coalescing_registry;

    // Initialize background worker resources
    runtime::WorkerQueueConfig wq_config{};
    wq_config.worker_count = 2;
    const char* wq_err = runtime::worker_queue_init(&impl->service.worker_queue, wq_config);
    if (wq_err != nullptr) {
        return false;
    }

    runtime::WorkerQueueResources wq_res{};
    wq_res.l1_cache = config.l1_cache;
    wq_res.l2_cache = config.l2_cache;
    wq_res.runtime_metrics =
        config.metrics_registry ? &config.metrics_registry->runtime_metrics : nullptr;
    WorkerQueueStartGuard worker_guard{};
    wq_err = runtime::worker_queue_start(&impl->service.worker_queue, wq_res);
    if (wq_err != nullptr) {
        return false;
    }
    worker_guard.arm(&impl->service.worker_queue);

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

    auto* impl = static_cast<GrpcServerImpl*>(handle->impl);
    if (impl->server) {
        impl->server->Shutdown();
        impl->server->Wait();
    }

    runtime::worker_queue_shutdown(&impl->service.worker_queue);

    delete impl;
    handle->impl = nullptr;
    handle->bound_port = 0;
}

} // namespace bytetaper::extproc

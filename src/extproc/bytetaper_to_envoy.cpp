// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "extproc/bytetaper_to_envoy.h"

#include "compression/compression_decision.h"
#include "extproc/reporting_headers.h"
#include "stages/compression_decision_stage.h"

#include <cstring>
#include <envoy/config/core/v3/base.pb.h>
#include <envoy/type/v3/http_status.pb.h>
#include <string>

namespace bytetaper::extproc {

static constexpr const char* kPaginationAppliedHeader = "x-bytetaper-pagination-applied";
static constexpr const char* kPaginationReasonHeader = "x-bytetaper-pagination-reason";
static constexpr const char* kPaginationLimitHeader = "x-bytetaper-pagination-limit";

constexpr const char* kCompressionCandidateHeader = "x-bytetaper-compression-candidate";
constexpr const char* kCompressionReasonHeader = "x-bytetaper-compression-reason";
constexpr const char* kCompressionAlgorithmHeader = "x-bytetaper-compression-algorithm-hint";

static void add_common_header(envoy::service::ext_proc::v3::CommonResponse* common, const char* key,
                              const std::string& value) {
    auto* mutation = common->mutable_header_mutation()->add_set_headers();
    mutation->mutable_header()->set_key(key);
    mutation->mutable_header()->set_raw_value(value);
    mutation->set_append_action(
        envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
}

static void add_header(envoy::service::ext_proc::v3::ImmediateResponse* imm, const char* key,
                       const std::string& value) {
    auto* mutation = imm->mutable_headers()->add_set_headers();
    mutation->mutable_header()->set_key(key);
    mutation->mutable_header()->set_raw_value(value);
    mutation->set_append_action(
        envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
}

bool map_cache_hit_to_immediate_response(
    apg::ApgTransformContext& ctx, envoy::service::ext_proc::v3::ProcessingResponse* response) {

    if (!ctx.should_return_immediate_response) {
        return false;
    }

    const cache::CacheEntry& entry = ctx.cached_response;
    auto* imm = response->mutable_immediate_response();

    imm->mutable_status()->set_code(static_cast<envoy::type::v3::StatusCode>(entry.status_code));

    if (entry.body != nullptr && entry.body_len > 0) {
        imm->set_body(std::string(entry.body, entry.body_len));
    }

    if (entry.content_type[0] != '\0') {
        add_header(imm, "content-type", entry.content_type);
    }

    add_header(imm, kCachedResponseHeader, kTrueValue);

    add_header(imm, kResponseBodyHeader, kTrueValue);

    const std::size_t saved =
        (entry.original_body_len > 0 && entry.original_body_len > entry.body_len)
            ? (entry.original_body_len - entry.body_len)
            : 0;
    add_header(imm, kWasteRemovedFieldsHeader, std::to_string(entry.removed_fields));
    add_header(imm, kWasteSavedBytesHeader, std::to_string(saved));
    add_header(
        imm, kOriginalResponseBytesHeader,
        std::to_string(entry.original_body_len > 0 ? entry.original_body_len : entry.body_len));
    add_header(imm, kOptimizedResponseBytesHeader, std::to_string(entry.body_len));
    add_header(imm, kTransformAppliedHeader, entry.removed_fields > 0 ? kTrueValue : kFalseValue);

    if (ctx.cache_layer != nullptr) {
        add_header(imm, kCacheLayerHeader, ctx.cache_layer);
    }

    if (ctx.matched_policy != nullptr && ctx.matched_policy->compression.enabled) {
        ctx.response_status_code = entry.status_code;
        std::strncpy(ctx.response_content_type, entry.content_type,
                     sizeof(ctx.response_content_type) - 1);
        ctx.response_content_type[sizeof(ctx.response_content_type) - 1] = '\0';
        ctx.response_content_type_len = std::strlen(ctx.response_content_type);
        ctx.response_body_len = entry.body_len;
        ctx.response_body_size_known = true;

        stages::compression_decision_stage(ctx);

        if (ctx.compression_decision.evaluated) {
            add_header(imm, kCompressionCandidateHeader,
                       ctx.compression_decision.candidate ? kTrueValue : kFalseValue);
            const char* reason_str = compression::compression_skip_reason_to_string(
                ctx.compression_decision.skip_reason);
            if (reason_str != nullptr) {
                add_header(imm, kCompressionReasonHeader, reason_str);
            }
            if (ctx.compression_decision.candidate) {
                const auto alg = ctx.compression_decision.algorithm_hint;
                const char* alg_str = (alg == policy::CompressionAlgorithm::Brotli) ? "br"
                                      : (alg == policy::CompressionAlgorithm::Zstd) ? "zstd"
                                      : (alg == policy::CompressionAlgorithm::Gzip) ? "gzip"
                                                                                    : nullptr;
                if (alg_str != nullptr) {
                    add_header(imm, kCompressionAlgorithmHeader, alg_str);
                }
            }
        }
    }

    return true;
}

void apply_pagination_response_headers(const apg::ApgTransformContext& ctx,
                                       envoy::service::ext_proc::v3::CommonResponse* common) {
    if (common == nullptr || !ctx.request_mutation.applied) {
        return;
    }
    add_common_header(common, kPaginationAppliedHeader, kTrueValue);
    if (ctx.request_mutation.reason != nullptr) {
        add_common_header(common, kPaginationReasonHeader, ctx.request_mutation.reason);
    }
    add_common_header(common, kPaginationLimitHeader,
                      std::to_string(ctx.request_mutation.limit_to_apply));
}

void apply_pagination_request_headers(const apg::ApgTransformContext& ctx,
                                      envoy::service::ext_proc::v3::CommonResponse* common) {
    if (common == nullptr || !ctx.request_mutation.applied) {
        return;
    }
    add_common_header(common, ":path", ctx.request_mutation.path);
}

void apply_compression_response_headers(const apg::ApgTransformContext& ctx,
                                        envoy::service::ext_proc::v3::CommonResponse* common) {
    apply_compression_decision_headers(ctx.compression_decision, common);
}

void apply_compression_decision_headers(const apg::CompressionDecisionOutput& decision,
                                        envoy::service::ext_proc::v3::CommonResponse* common) {
    if (common == nullptr || !decision.evaluated) {
        return;
    }
    add_common_header(common, kCompressionCandidateHeader,
                      decision.candidate ? kTrueValue : kFalseValue);
    const char* reason_str = compression::compression_skip_reason_to_string(decision.skip_reason);
    if (reason_str != nullptr) {
        add_common_header(common, kCompressionReasonHeader, reason_str);
    }
    if (decision.candidate) {
        const auto alg = decision.algorithm_hint;
        const char* alg_str = (alg == policy::CompressionAlgorithm::Brotli) ? "br"
                              : (alg == policy::CompressionAlgorithm::Zstd) ? "zstd"
                              : (alg == policy::CompressionAlgorithm::Gzip) ? "gzip"
                                                                            : nullptr;
        if (alg_str != nullptr) {
            add_common_header(common, kCompressionAlgorithmHeader, alg_str);
        }
    }
}

} // namespace bytetaper::extproc

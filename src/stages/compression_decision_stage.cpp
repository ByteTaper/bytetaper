// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "stages/compression_decision_stage.h"

#include "compression/compression_decision.h"

#include <cstring>

namespace bytetaper::stages {

apg::StageOutput compression_decision_stage(apg::ApgTransformContext& context) {
    if (context.matched_policy == nullptr) {
        return { apg::StageResult::Continue, "no-policy" };
    }

    const auto& comp = context.matched_policy->compression;
    if (!comp.enabled) {
        if (context.compression_metrics != nullptr) {
            metrics::record_compression_event(context.compression_metrics,
                                              metrics::CompressionMetricEvent::SkipPolicyDisabled);
        }
        return { apg::StageResult::Continue, "compression-disabled" };
    }

    compression::CompressionDecisionInput input{};
    input.compression_policy = &comp;
    input.client_encoding = context.client_accept_encoding;
    input.response_encoding = context.response_content_encoding;
    input.status_code = context.response_status_code;
    input.content_type = context.response_content_type;
    input.content_type_len = context.response_content_type_len;
    input.body_len = context.response_body_len;
    input.body_size_known = context.response_body_len > 0;

    const auto decision = compression::make_compression_decision(input);

    context.compression_decision.evaluated = true;
    context.compression_decision.candidate = decision.candidate;
    context.compression_decision.skip_reason = decision.skip_reason;
    context.compression_decision.algorithm_hint = decision.selected_algorithm_hint;

    if (context.compression_metrics != nullptr) {
        if (decision.candidate) {
            metrics::record_compression_event(context.compression_metrics,
                                              metrics::CompressionMetricEvent::Candidate);
        } else {
            switch (decision.skip_reason) {
            case compression::CompressionSkipReason::PolicyDisabled:
                metrics::record_compression_event(
                    context.compression_metrics,
                    metrics::CompressionMetricEvent::SkipPolicyDisabled);
                break;
            case compression::CompressionSkipReason::NoClientSupport:
                metrics::record_compression_event(
                    context.compression_metrics,
                    metrics::CompressionMetricEvent::SkipClientUnsupported);
                break;
            case compression::CompressionSkipReason::AlreadyEncoded:
                metrics::record_compression_event(
                    context.compression_metrics,
                    metrics::CompressionMetricEvent::SkipAlreadyEncoded);
                break;
            case compression::CompressionSkipReason::ContentTypeNotEligible:
                metrics::record_compression_event(context.compression_metrics,
                                                  metrics::CompressionMetricEvent::SkipContentType);
                break;
            case compression::CompressionSkipReason::BelowMinimum:
            case compression::CompressionSkipReason::SizeUnknown:
                metrics::record_compression_event(
                    context.compression_metrics,
                    metrics::CompressionMetricEvent::SkipResponseTooSmall);
                break;
            case compression::CompressionSkipReason::Non2xxStatus:
                metrics::record_compression_event(context.compression_metrics,
                                                  metrics::CompressionMetricEvent::SkipNon2xx);
                break;
            default:
                break;
            }
        }
    }

    return { apg::StageResult::Continue,
             decision.candidate
                 ? "compression-candidate"
                 : compression::compression_skip_reason_to_string(decision.skip_reason) };
}

} // namespace bytetaper::stages

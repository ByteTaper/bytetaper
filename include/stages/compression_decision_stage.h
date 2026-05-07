// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "apg/context.h"
#include "apg/stage.h"

namespace bytetaper::stages {

struct CompressionDecisionContext {
    const policy::RoutePolicy* matched_policy = nullptr;
    compression::AcceptEncoding client_accept_encoding{};
    compression::ContentEncodingResult response_content_encoding{};
    std::uint16_t response_status_code = 0;
    const char* response_content_type = nullptr;
    std::size_t response_content_type_len = 0;
    std::size_t response_body_len = 0;
    bool response_body_size_known = false;
    apg::CompressionDecisionOutput compression_decision{};
    metrics::CompressionMetrics* compression_metrics = nullptr;
};

apg::StageOutput compression_decision_stage(apg::ApgTransformContext& context);

apg::StageOutput evaluate_compression_decision_fast(CompressionDecisionContext& context);

} // namespace bytetaper::stages

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#pragma once

#include "apg/context.h"

#include <envoy/service/ext_proc/v3/external_processor.pb.h>

namespace bytetaper::extproc {

// Fills `response` with an ImmediateResponse built from ctx.cached_response.
// Returns true if ctx.should_return_immediate_response is set; false otherwise.
// Safe to call unconditionally — caller skips upstream processing on true.
bool map_cache_hit_to_immediate_response(
    apg::ApgTransformContext& ctx, envoy::service::ext_proc::v3::ProcessingResponse* response);

// Writes pagination diagnostic headers to common when context.request_mutation.applied.
// No-op when mutation was not applied (headers absent = no mutation, per contract).
void apply_pagination_response_headers(const apg::ApgTransformContext& ctx,
                                       envoy::service::ext_proc::v3::CommonResponse* common);

// Writes mutated :path header to common when context.request_mutation.applied.
void apply_pagination_request_headers(const apg::ApgTransformContext& ctx,
                                      envoy::service::ext_proc::v3::CommonResponse* common);

void apply_compression_response_headers(const apg::ApgTransformContext& ctx,
                                        envoy::service::ext_proc::v3::CommonResponse* common);

void apply_compression_decision_headers(const apg::CompressionDecisionOutput& decision,
                                        envoy::service::ext_proc::v3::CommonResponse* common);

} // namespace bytetaper::extproc

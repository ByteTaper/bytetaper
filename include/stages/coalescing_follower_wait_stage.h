// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_STAGES_COALESCING_FOLLOWER_WAIT_STAGE_H
#define BYTETAPER_STAGES_COALESCING_FOLLOWER_WAIT_STAGE_H

#include "apg/context.h"
#include "apg/result.h"

namespace bytetaper::stages {

// APG pipeline stage: follower wait — polls L1 cache until hit or timeout.
// L2/RocksDB is never consulted; async L2-to-L1 promotion lets
// followers observe L2 results via L1 poll.
// On L1 hit: returns StageResult::SkipRemaining.
// On timeout: falls back upstream, returns StageResult::Continue.
apg::StageOutput coalescing_follower_wait_stage(apg::ApgTransformContext& context);

} // namespace bytetaper::stages

#endif // BYTETAPER_STAGES_COALESCING_FOLLOWER_WAIT_STAGE_H

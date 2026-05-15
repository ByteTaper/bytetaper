// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_STAGES_MUTATION_INVALIDATION_APPLY_STAGE_H
#define BYTETAPER_STAGES_MUTATION_INVALIDATION_APPLY_STAGE_H

#include "apg/context.h"
#include "apg/stage.h"

namespace bytetaper::stages {

/**
 * Mutation Invalidation Apply Stage.
 *
 * Runs during the response-header or response-body phase.
 * If a "pending invalidation plan" was prepared by the prepare stage, this stage
 * checks if the upstream response was successful (e.g., 2xx status).
 * If so, it executes the epoch increments (bumps) in the RouteCacheEpochStore
 * for all targeted routes.
 */
apg::StageOutput mutation_invalidation_apply_stage(apg::ApgTransformContext& context);

} // namespace bytetaper::stages

#endif // BYTETAPER_STAGES_MUTATION_INVALIDATION_APPLY_STAGE_H

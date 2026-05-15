// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_STAGES_MUTATION_INVALIDATION_PREPARE_STAGE_H
#define BYTETAPER_STAGES_MUTATION_INVALIDATION_PREPARE_STAGE_H

#include "apg/context.h"
#include "apg/stage.h"

namespace bytetaper::stages {

/**
 * Mutation Invalidation Prepare Stage.
 *
 * Runs during the request-header phase (lookup).
 * It detects if the current request is a state-mutating operation (PATCH/PUT/DELETE)
 * that matches a route policy with cache invalidation enabled.
 * If so, it populates a "pending invalidation plan" in the context, which will be
 * executed by the apply stage in the response phase if the upstream call is successful.
 */
apg::StageOutput mutation_invalidation_prepare_stage(apg::ApgTransformContext& context);

} // namespace bytetaper::stages

#endif // BYTETAPER_STAGES_MUTATION_INVALIDATION_PREPARE_STAGE_H

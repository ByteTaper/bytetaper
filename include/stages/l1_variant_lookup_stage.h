// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_STAGES_L1_VARIANT_LOOKUP_STAGE_H
#define BYTETAPER_STAGES_L1_VARIANT_LOOKUP_STAGE_H

#include "apg/context.h"
#include "apg/result.h"

namespace bytetaper::stages {

apg::StageOutput l1_variant_lookup_stage(apg::ApgTransformContext& context);

} // namespace bytetaper::stages

#endif // BYTETAPER_STAGES_L1_VARIANT_LOOKUP_STAGE_H

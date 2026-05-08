// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_STAGES_FIELD_VARIANT_ADMISSION_STAGE_H
#define BYTETAPER_STAGES_FIELD_VARIANT_ADMISSION_STAGE_H

#include "apg/context.h"
#include "apg/result.h"

namespace bytetaper::stages {

// APG pipeline stage: checks variant cardinality and count thresholds before storing variant in
// cache.
apg::StageOutput field_variant_admission_stage(apg::ApgTransformContext& context);

} // namespace bytetaper::stages

#endif // BYTETAPER_STAGES_FIELD_VARIANT_ADMISSION_STAGE_H

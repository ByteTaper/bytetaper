// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_TQ_DRY_RUN_REPORTER_H
#define BYTETAPER_TAPERQUERY_TQ_DRY_RUN_REPORTER_H

#include "taperquery/tq_plan.h"

#include <string>

namespace bytetaper::taperquery {

/**
 * Renders the deterministic plain-text dry-run report for a TqPolicyChangePlan.
 */
std::string render_taperquery_dry_run_text(const TqPolicyChangePlan& plan);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_TQ_DRY_RUN_REPORTER_H

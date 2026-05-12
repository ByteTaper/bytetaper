// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_NORMALIZE_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_NORMALIZE_H

#include "taperquery/policy_ir.h"

namespace bytetaper::taperquery {

TqPolicyDocument normalize_policy_ir(const TqPolicyDocument& input);
TqRoutePolicy normalize_route_policy_ir(const TqRoutePolicy& input);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_NORMALIZE_H

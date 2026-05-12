// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_HASH_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_HASH_H

#include "taperquery/policy_ir.h"

#include <string>

namespace bytetaper::taperquery {

std::string compute_policy_document_identity(const TqPolicyDocument& policy);
std::string compute_route_policy_identity(const TqRoutePolicy& route);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_HASH_H

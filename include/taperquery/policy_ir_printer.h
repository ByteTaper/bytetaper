// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_PRINTER_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_PRINTER_H

#include "taperquery/policy_ir.h"

#include <string>

namespace bytetaper::taperquery {

/**
 * Returns a deterministic YAML-like printed string of a given TqPolicyDocument.
 * Fields with default/empty values are omitted for readability.
 */
std::string print_canonical_policy_ir(const TqPolicyDocument& policy);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_PRINTER_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_TAPERQUERY_POLICY_IR_IDENTITY_H
#define BYTETAPER_TAPERQUERY_POLICY_IR_IDENTITY_H

#include "taperquery/policy_ir.h"
#include "taperquery/policy_ir_version.h"

#include <string>
#include <string_view>

namespace bytetaper::taperquery {

/**
 * @brief Computes the deterministic route-level content identity hash.
 *
 * Route identity includes the following semantic fields (after internal normalization):
 * - Route ID (`route_id`)
 * - Match kind (`match_kind`)
 * - Match path (`match_prefix`)
 * - Allowed HTTP method (`allowed_method`)
 * - Mutation mode (`mutation`)
 * - Failure mode (`failure_mode`)
 * - Max response bytes limit (`max_response_bytes`)
 * - Field filtering configurations (mode and field list in stable order)
 * - Cache enabling, layer-specific flags, capacities, paths, privacy setting, vary headers, and
 * variant controls
 * - Pagination settings (limit, offset, param names, limits)
 * - Compression settings (algorithm preference, content types)
 * - Coalescing timing, buffer budgets, and concurrency caps
 *
 * @param route The route policy configuration to hash.
 * @return 16-character lowercase hexadecimal SipHash-2-4 string.
 */
std::string compute_route_policy_identity(const TqRoutePolicy& route);

/**
 * @brief Computes the deterministic top-level document semantic content identity hash.
 *
 * Document content identity includes:
 * - Policy IR version (`policy_ir_version`)
 * - Identity version (`identity_version`)
 * - Document ID (`document_id`)
 * - Routes count
 * - Routes list identities computed in declaration order
 *
 * The document content identity strictly excludes non-semantic metadata and provenance:
 * - Source filename / YAML path (`source_name`)
 * - Top-level or sub-level comments or format styling
 * - Base repository commit hash (`expected_base_sha`)
 * - Legacy schema version (`schema_version`) or YAML loader source version
 * (`source_schema_version`)
 *
 * @param policy The policy document to hash.
 * @return 16-character lowercase hexadecimal SipHash-2-4 string.
 */
std::string compute_policy_document_identity(const TqPolicyDocument& policy);

/**
 * @brief Computes the apply request boundary identity hash (combines semantic content and base
 * SHA).
 *
 * Differs from compute_policy_document_identity by incorporating the `expected_base_sha`
 * field to guarantee secure CAS boundaries at apply time.
 *
 * @param policy The policy document to hash.
 * @return 16-character lowercase hexadecimal SipHash-2-4 string.
 */
std::string compute_policy_apply_request_identity(const TqPolicyDocument& policy);

} // namespace bytetaper::taperquery

#endif // BYTETAPER_TAPERQUERY_POLICY_IR_IDENTITY_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_POLICY_POLICY_IDENTITY_H
#define BYTETAPER_POLICY_POLICY_IDENTITY_H

#include "policy/route_policy.h"

#include <cstddef>

namespace bytetaper::policy {

static constexpr std::size_t kPolicyIdentityV2MaxLen = 96;

struct PolicyIdentityV2 {
    char value[kPolicyIdentityV2MaxLen] = {};
};

bool compute_route_policy_identity_v2(const RoutePolicy& route, PolicyIdentityV2* out);

bool compute_policy_file_identity_v2(const RoutePolicy* routes, std::size_t route_count,
                                     PolicyIdentityV2* out);

bool copy_route_policy_identity_v2_to_legacy_slot(RoutePolicy* route);

} // namespace bytetaper::policy

#endif // BYTETAPER_POLICY_POLICY_IDENTITY_H

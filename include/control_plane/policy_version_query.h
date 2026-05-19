// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_VERSION_QUERY_H
#define BYTETAPER_CONTROL_PLANE_POLICY_VERSION_QUERY_H

#include "control_plane/policy_apply_status.h"
#include "control_plane/policy_state_record.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bytetaper::control_plane {

struct PolicyVersionSummary {
    std::uint64_t generation = 0;
    std::string policy_id;
    std::string canonical_hash;
    std::string source_type;
    std::uint64_t created_at_unix_epoch_ms = 0;
};

struct PolicyVersionListResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    std::string resource_key;
    std::vector<PolicyVersionSummary> versions;
    std::string error;
};

struct PolicyVersionFetchResult {
    bool ok = false;
    PolicyApplyStatus status = PolicyApplyStatus::InternalError;
    PolicyVersionRecord record{};
    std::string canonical_yaml;
    std::string error;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_VERSION_QUERY_H

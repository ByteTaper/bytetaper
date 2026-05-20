// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_RUNTIME_LOCAL_POLICY_FETCH_H
#define BYTETAPER_RUNTIME_POLICY_RUNTIME_LOCAL_POLICY_FETCH_H

#include "control_plane/manual_resolution_api.h"
#include "control_plane/runtime_status_report.h"
#include "taperquery/policy_persistence.h"

namespace bytetaper::runtime_policy {

control_plane::AdoptLocalFetchedPolicy
fetch_local_policy_from_mirror(const taperquery::LocalPolicyPersistenceConfig& mirror_config,
                               const control_plane::RuntimeStatusReport& runtime_report);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_RUNTIME_LOCAL_POLICY_FETCH_H

// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_RUNTIME_POLICY_BOOTSTRAP_POLICY_IMPORTER_H
#define BYTETAPER_RUNTIME_POLICY_BOOTSTRAP_POLICY_IMPORTER_H

#include "control_plane/policy_state_key.h"
#include "control_plane/policy_state_store.h"
#include "runtime/policy_snapshot.h"
#include "runtime_policy/runtime_policy_status.h"
#include "taperquery/policy_persistence.h"

#include <memory>

namespace bytetaper::runtime_policy {

struct BootstrapImportInput {
    const char* bootstrap_policy_file = nullptr;
    control_plane::PolicyStateStore* store = nullptr;
    const taperquery::LocalPolicyPersistenceConfig* persistence_config = nullptr;
    const control_plane::PolicyResourceKey* resource_key = nullptr;
    bool confirm_import = false;
};

struct BootstrapImportResult {
    bool ok = false;
    RuntimePolicyHealth health = RuntimePolicyHealth::Unknown;
    std::shared_ptr<const runtime::RuntimePolicySnapshot> snapshot;
    std::string error;
};

RuntimePolicyHealth check_bootstrap_drift(const BootstrapImportInput& input,
                                          const control_plane::ActivePolicyPointer& committed);

BootstrapImportResult import_bootstrap_policy(const BootstrapImportInput& input);

} // namespace bytetaper::runtime_policy

#endif // BYTETAPER_RUNTIME_POLICY_BOOTSTRAP_POLICY_IMPORTER_H

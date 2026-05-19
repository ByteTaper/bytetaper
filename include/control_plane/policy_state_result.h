// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#ifndef BYTETAPER_CONTROL_PLANE_POLICY_STATE_RESULT_H
#define BYTETAPER_CONTROL_PLANE_POLICY_STATE_RESULT_H

#include "control_plane/policy_state_record.h"

#include <string>

namespace bytetaper::control_plane {

enum class PolicyStateErrorCode {
    Ok,
    DbOpenFailed,
    DbWriteFailed,
    DbReadFailed,
    VersionNotFound,
    VersionConflict,
    ActivePointerNotFound,
    ActivePointerConflict,
    ActivePointerInvalid,
    ActivePointerTargetMissing,
    AuditWriteFailed,
    JobNotFound,
    JobWriteFailed,
};

struct StorePolicyVersionResult {
    bool ok = false;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

struct LoadPolicyVersionResult {
    bool ok = false;
    PolicyVersionRecord record;
    std::string canonical_yaml;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

struct LoadActivePointerResult {
    bool ok = false;
    bool not_found = false;
    ActivePolicyPointer pointer;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

struct PromoteActiveResult {
    bool ok = false;
    bool idempotent = false;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

struct AppendAuditResult {
    bool ok = false;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

struct StorePolicyUpdateJobResult {
    bool ok = false;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

struct LoadPolicyUpdateJobResult {
    bool ok = false;
    bool not_found = false;
    PolicyUpdateJobRecord record;
    PolicyStateErrorCode code = PolicyStateErrorCode::Ok;
    std::string error;
};

} // namespace bytetaper::control_plane

#endif // BYTETAPER_CONTROL_PLANE_POLICY_STATE_RESULT_H

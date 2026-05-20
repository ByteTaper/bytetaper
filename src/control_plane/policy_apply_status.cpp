// SPDX-FileCopyrightText: 2026 Haluan Irsad
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial

#include "control_plane/policy_apply_status.h"

namespace bytetaper::control_plane {

const char* to_string(PolicyApplyStatus status) {
    switch (status) {
    case PolicyApplyStatus::DryRunReady:
        return "DryRunReady";
    case PolicyApplyStatus::DiffReady:
        return "DiffReady";
    case PolicyApplyStatus::Accepted:
        return "Accepted";
    case PolicyApplyStatus::Applied:
        return "Applied";
    case PolicyApplyStatus::PolicyCommittedNotActivated:
        return "PolicyCommittedNotActivated";
    case PolicyApplyStatus::PolicyActivationInProgress:
        return "PolicyActivationInProgress";
    case PolicyApplyStatus::PolicyActivationFailed:
        return "PolicyActivationFailed";
    case PolicyApplyStatus::PolicyActiveCleanupPending:
        return "PolicyActiveCleanupPending";
    case PolicyApplyStatus::PolicyActive:
        return "PolicyActive";
    case PolicyApplyStatus::RejectedInvalidRequest:
        return "RejectedInvalidRequest";
    case PolicyApplyStatus::RejectedParseError:
        return "RejectedParseError";
    case PolicyApplyStatus::RejectedCompileError:
        return "RejectedCompileError";
    case PolicyApplyStatus::RejectedValidation:
        return "RejectedValidation";
    case PolicyApplyStatus::RejectedCasMismatch:
        return "RejectedCasMismatch";
    case PolicyApplyStatus::RejectedNoChanges:
        return "RejectedNoChanges";
    case PolicyApplyStatus::RejectedPolicyInactive:
        return "RejectedPolicyInactive";
    case PolicyApplyStatus::RejectedQueueFull:
        return "RejectedQueueFull";
    case PolicyApplyStatus::RejectedStorageUnavailable:
        return "RejectedStorageUnavailable";
    case PolicyApplyStatus::RollbackPlanReady:
        return "RollbackPlanReady";
    case PolicyApplyStatus::RollbackAccepted:
        return "RollbackAccepted";
    case PolicyApplyStatus::RepairLocalAccepted:
        return "RepairLocalAccepted";
    case PolicyApplyStatus::RepairLocalPlanReady:
        return "RepairLocalPlanReady";
    case PolicyApplyStatus::AdoptLocalAccepted:
        return "AdoptLocalAccepted";
    case PolicyApplyStatus::AdoptLocalPlanReady:
        return "AdoptLocalPlanReady";
    case PolicyApplyStatus::InternalError:
        return "InternalError";
    }
    return "Unknown";
}

} // namespace bytetaper::control_plane
